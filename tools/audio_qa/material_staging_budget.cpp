#include "material_staging_budget.h"

#include "model_limits.h"

#include <limits>
#include <set>
#include <string>

namespace ayther::audio_qa {
namespace {

bool identifier(const std::string_view value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}

bool add(const std::uint64_t left, const std::uint64_t right, std::uint64_t &result) noexcept {
    if (right > (std::numeric_limits<std::uint64_t>::max)() - left) {
        return false;
    }
    result = left + right;
    return true;
}

MaterialStagingRejection rejection(const std::string_view run_id,
                                   const MaterialStagingRejectionReason reason,
                                   const std::uint64_t selected_bytes,
                                   const std::uint64_t bytes_to_allocate,
                                   const std::uint64_t required_free_bytes) {
    Diagnostic diagnostic;
    diagnostic.code = std::string{material_staging_rejection_code(reason)};
    diagnostic.scope = DiagnosticScope::storage;
    diagnostic.severity = DiagnosticSeverity::error;
    diagnostic.subject_id = run_id;
    diagnostic.stage = "material_staging";
    diagnostic.affected_samples = {Availability::not_applicable, std::nullopt,
                                   "replay_not_started"};
    diagnostic.last_confirmed_frame = {Availability::not_applicable, std::nullopt,
                                       "replay_not_started"};
    diagnostic.lost_fact_count = {Availability::not_applicable, std::nullopt,
                                  "capture_not_started"};
    diagnostic.detail = reason == MaterialStagingRejectionReason::private_material_limit_exceeded
                            ? "selected private materials exceed the 8 GiB request limit; no "
                              "additional material was staged"
                            : "free storage is below the selected bytes still to allocate plus "
                              "the 2 GiB staging margin and 10 GiB evidence reserve";
    return {reason,         EvidenceResult::incomplete, std::move(diagnostic),
            selected_bytes, bytes_to_allocate,          required_free_bytes};
}

} // namespace

MaterialStagingCheckResult
check_material_staging_budget(const std::string_view run_id,
                              const std::span<const MaterialStagingItem> inventory,
                              const std::uint64_t available_bytes) noexcept {
    try {
        if (!identifier(run_id)) {
            return MaterialStagingCheckError::invalid_run_id;
        }
        if (inventory.empty()) {
            return MaterialStagingCheckError::invalid_inventory;
        }
        if (inventory.size() > max_reference_materials) {
            return MaterialStagingCheckError::capacity_exceeded;
        }

        std::set<std::string_view> identities;
        std::uint64_t selected_bytes{};
        std::uint64_t already_staged_bytes{};
        std::uint64_t bytes_to_allocate{};
        for (const auto &item : inventory) {
            if (!identifier(item.material_id) || item.byte_size == 0 ||
                (!item.selected && item.already_staged)) {
                return MaterialStagingCheckError::invalid_inventory;
            }
            if (!identities.insert(item.material_id).second) {
                return MaterialStagingCheckError::duplicate_material;
            }
            if (!item.selected) {
                continue;
            }
            if (!add(selected_bytes, item.byte_size, selected_bytes)) {
                return MaterialStagingCheckError::invalid_inventory;
            }
            auto &destination = item.already_staged ? already_staged_bytes : bytes_to_allocate;
            if (!add(destination, item.byte_size, destination)) {
                return MaterialStagingCheckError::invalid_inventory;
            }
        }
        if (selected_bytes == 0) {
            return MaterialStagingCheckError::invalid_inventory;
        }

        constexpr auto fixed_reserve =
            private_material_staging_margin_bytes + material_evidence_reserve_bytes;
        std::uint64_t required_free_bytes{};
        if (!add(bytes_to_allocate, fixed_reserve, required_free_bytes)) {
            return MaterialStagingCheckError::invalid_inventory;
        }
        if (selected_bytes > max_pinned_private_material_bytes) {
            return rejection(run_id,
                             MaterialStagingRejectionReason::private_material_limit_exceeded,
                             selected_bytes, bytes_to_allocate, required_free_bytes);
        }
        if (available_bytes < required_free_bytes) {
            return rejection(run_id, MaterialStagingRejectionReason::storage_reserve_unavailable,
                             selected_bytes, bytes_to_allocate, required_free_bytes);
        }
        return MaterialStagingPermit{selected_bytes, already_staged_bytes, bytes_to_allocate,
                                     required_free_bytes};
    } catch (...) {
        return MaterialStagingCheckError::invalid_inventory;
    }
}

std::string_view
material_staging_rejection_code(const MaterialStagingRejectionReason reason) noexcept {
    switch (reason) {
    case MaterialStagingRejectionReason::private_material_limit_exceeded:
        return "private_material_limit_exceeded";
    case MaterialStagingRejectionReason::storage_reserve_unavailable:
        return "material_storage_reserve_unavailable";
    }
    return "material_staging_unknown";
}

} // namespace ayther::audio_qa
