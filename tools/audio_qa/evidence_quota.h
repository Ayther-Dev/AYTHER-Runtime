#pragma once

#include "coverage_model.h"
#include "model.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace ayther::audio_qa {

inline constexpr std::uint64_t max_evidence_bytes = std::uint64_t{8} << 30U;
inline constexpr std::uint64_t evidence_terminal_reserve_bytes = std::uint64_t{1} << 20U;
inline constexpr std::uint64_t minimum_initial_evidence_free_bytes = std::uint64_t{10} << 30U;

struct EvidenceByteUsage {
    std::uint64_t durable_bytes{};
    std::uint64_t temporary_bytes{};
    std::uint64_t in_flight_bytes{};
    bool operator==(const EvidenceByteUsage &) const = default;
};

struct EvidenceQuotaUsage {
    EvidenceByteUsage regular;
    EvidenceByteUsage terminal;
    std::optional<std::uint64_t> last_checkpoint_sequence;
    bool operator==(const EvidenceQuotaUsage &) const = default;
};

enum class EvidenceWriteClass { regular, terminal };

enum class EvidenceQuotaFailureReason {
    initial_reserve_unavailable,
    evidence_limit_reached,
    terminal_reserve_exhausted,
    storage_unavailable,
};

enum class EvidenceQuotaCheckError {
    invalid_run_id,
    invalid_request,
    invalid_usage,
};

struct EvidenceQuotaPermit {
    std::uint64_t accounted_bytes{};
    std::uint64_t remaining_regular_bytes{};
    std::uint64_t remaining_terminal_bytes{};
    bool operator==(const EvidenceQuotaPermit &) const = default;
};

struct EvidenceQuotaFailure {
    EvidenceQuotaFailureReason reason{EvidenceQuotaFailureReason::initial_reserve_unavailable};
    EvidenceResult evidence_result{EvidenceResult::incomplete};
    Diagnostic diagnostic;
    std::optional<std::uint64_t> preserved_checkpoint_sequence;
    std::uint64_t accounted_bytes{};
    std::uint64_t remaining_regular_bytes{};
    std::uint64_t remaining_terminal_bytes{};
    bool operator==(const EvidenceQuotaFailure &) const = default;
};

using EvidenceQuotaCheckResult =
    std::variant<EvidenceQuotaPermit, EvidenceQuotaFailure, EvidenceQuotaCheckError>;

[[nodiscard]] EvidenceQuotaCheckResult
check_initial_evidence_reserve(std::string_view run_id, std::uint64_t available_bytes) noexcept;

[[nodiscard]] EvidenceQuotaCheckResult check_evidence_write(std::string_view run_id,
                                                            const EvidenceQuotaUsage &usage,
                                                            EvidenceWriteClass write_class,
                                                            std::uint64_t requested_bytes,
                                                            std::uint64_t available_bytes) noexcept;

[[nodiscard]] std::string_view
evidence_quota_failure_code(EvidenceQuotaFailureReason reason) noexcept;

} // namespace ayther::audio_qa
