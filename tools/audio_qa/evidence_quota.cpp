#include "evidence_quota.h"

#include "model_limits.h"

#include <limits>
#include <string>

namespace ayther::audio_qa {
namespace {

struct AccountedUsage {
    std::uint64_t regular{};
    std::uint64_t terminal{};
    std::uint64_t total{};
};

bool valid_run_id(const std::string_view value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}

bool add(const std::uint64_t left, const std::uint64_t right, std::uint64_t &result) noexcept {
    if (right > (std::numeric_limits<std::uint64_t>::max)() - left) {
        return false;
    }
    result = left + right;
    return true;
}

std::optional<std::uint64_t> sum(const EvidenceByteUsage &usage) noexcept {
    std::uint64_t partial{};
    std::uint64_t total{};
    if (!add(usage.durable_bytes, usage.temporary_bytes, partial) ||
        !add(partial, usage.in_flight_bytes, total)) {
        return std::nullopt;
    }
    return total;
}

std::optional<AccountedUsage> account(const EvidenceQuotaUsage &usage) noexcept {
    const auto regular = sum(usage.regular);
    const auto terminal = sum(usage.terminal);
    std::uint64_t total{};
    if (!regular || !terminal || !add(*regular, *terminal, total) ||
        *regular > max_evidence_bytes - evidence_terminal_reserve_bytes ||
        *terminal > evidence_terminal_reserve_bytes || total > max_evidence_bytes ||
        (usage.last_checkpoint_sequence && *usage.last_checkpoint_sequence == 0)) {
        return std::nullopt;
    }
    return AccountedUsage{*regular, *terminal, total};
}

EvidenceQuotaPermit permit(const AccountedUsage usage) noexcept {
    return {usage.total, max_evidence_bytes - evidence_terminal_reserve_bytes - usage.regular,
            evidence_terminal_reserve_bytes - usage.terminal};
}

EvidenceQuotaFailure failure(const std::string_view run_id, const EvidenceQuotaFailureReason reason,
                             const AccountedUsage usage, const EvidenceQuotaUsage &reported_usage,
                             std::string detail) {
    Diagnostic diagnostic;
    diagnostic.code = std::string{evidence_quota_failure_code(reason)};
    diagnostic.scope = DiagnosticScope::storage;
    diagnostic.severity = DiagnosticSeverity::error;
    diagnostic.subject_id = run_id;
    diagnostic.stage = reason == EvidenceQuotaFailureReason::initial_reserve_unavailable
                           ? "evidence_admission"
                           : "evidence_capture";
    diagnostic.affected_samples = {Availability::unknown, std::nullopt,
                                   "storage_limit_has_no_sample_boundary"};
    diagnostic.last_confirmed_frame = {Availability::unknown, std::nullopt,
                                       "last_frame_is_preserved_by_the_run_checkpoint"};
    diagnostic.lost_fact_count = {Availability::unknown, std::nullopt,
                                  "capture_stopped_before_unaccounted_data_was_accepted"};
    diagnostic.detail = std::move(detail);
    return {reason,
            EvidenceResult::incomplete,
            std::move(diagnostic),
            reported_usage.last_checkpoint_sequence,
            usage.total,
            max_evidence_bytes - evidence_terminal_reserve_bytes - usage.regular,
            evidence_terminal_reserve_bytes - usage.terminal};
}

} // namespace

EvidenceQuotaCheckResult
check_initial_evidence_reserve(const std::string_view run_id,
                               const std::uint64_t available_bytes) noexcept {
    try {
        if (!valid_run_id(run_id)) {
            return EvidenceQuotaCheckError::invalid_run_id;
        }
        const EvidenceQuotaUsage empty_usage;
        const AccountedUsage empty;
        if (available_bytes < minimum_initial_evidence_free_bytes) {
            return failure(run_id, EvidenceQuotaFailureReason::initial_reserve_unavailable, empty,
                           empty_usage,
                           "initial free space is below the 10 GiB evidence reserve; capture "
                           "was not admitted");
        }
        return permit(empty);
    } catch (...) {
        return EvidenceQuotaCheckError::invalid_request;
    }
}

EvidenceQuotaCheckResult check_evidence_write(const std::string_view run_id,
                                              const EvidenceQuotaUsage &usage,
                                              const EvidenceWriteClass write_class,
                                              const std::uint64_t requested_bytes,
                                              const std::uint64_t available_bytes) noexcept {
    try {
        if (!valid_run_id(run_id)) {
            return EvidenceQuotaCheckError::invalid_run_id;
        }
        if (requested_bytes == 0) {
            return EvidenceQuotaCheckError::invalid_request;
        }
        const auto accounted = account(usage);
        if (!accounted) {
            return EvidenceQuotaCheckError::invalid_usage;
        }

        std::uint64_t regular_after = accounted->regular;
        std::uint64_t terminal_after = accounted->terminal;
        auto &class_after =
            write_class == EvidenceWriteClass::regular ? regular_after : terminal_after;
        if (!add(class_after, requested_bytes, class_after)) {
            return EvidenceQuotaCheckError::invalid_request;
        }

        std::uint64_t total_after{};
        if (!add(regular_after, terminal_after, total_after)) {
            return EvidenceQuotaCheckError::invalid_request;
        }
        if (write_class == EvidenceWriteClass::regular &&
            regular_after > max_evidence_bytes - evidence_terminal_reserve_bytes) {
            return failure(run_id, EvidenceQuotaFailureReason::evidence_limit_reached, *accounted,
                           usage,
                           "regular evidence reached its limit; the terminal 1 MiB remains "
                           "reserved for the diagnostic and final checkpoint");
        }
        if (terminal_after > evidence_terminal_reserve_bytes) {
            return failure(run_id, EvidenceQuotaFailureReason::terminal_reserve_exhausted,
                           *accounted, usage,
                           "the requested terminal evidence exceeds the reserved 1 MiB");
        }
        if (total_after > max_evidence_bytes) {
            return failure(run_id, EvidenceQuotaFailureReason::evidence_limit_reached, *accounted,
                           usage,
                           "the requested write would exceed the 8 GiB evidence "
                           "limit");
        }
        if (available_bytes < requested_bytes) {
            return failure(run_id, EvidenceQuotaFailureReason::storage_unavailable, *accounted,
                           usage,
                           "available storage cannot hold the requested evidence write; "
                           "capture stopped before accepting it");
        }
        return permit({regular_after, terminal_after, total_after});
    } catch (...) {
        return EvidenceQuotaCheckError::invalid_request;
    }
}

std::string_view evidence_quota_failure_code(const EvidenceQuotaFailureReason reason) noexcept {
    switch (reason) {
    case EvidenceQuotaFailureReason::initial_reserve_unavailable:
        return "evidence_initial_reserve_unavailable";
    case EvidenceQuotaFailureReason::evidence_limit_reached:
        return "evidence_quota_reached";
    case EvidenceQuotaFailureReason::terminal_reserve_exhausted:
        return "evidence_terminal_reserve_exhausted";
    case EvidenceQuotaFailureReason::storage_unavailable:
        return "evidence_storage_unavailable";
    }
    return "evidence_quota_unknown";
}

} // namespace ayther::audio_qa
