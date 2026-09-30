#pragma once

#include "coverage_model.h"
#include "model.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace ayther::audio_qa {

inline constexpr std::uint64_t max_pinned_private_material_bytes = std::uint64_t{8} << 30U;
inline constexpr std::uint64_t private_material_staging_margin_bytes = std::uint64_t{2} << 30U;
inline constexpr std::uint64_t material_evidence_reserve_bytes = std::uint64_t{10} << 30U;

struct MaterialStagingItem {
    std::string material_id;
    std::uint64_t byte_size{};
    bool selected{};
    bool already_staged{};
    bool operator==(const MaterialStagingItem &) const = default;
};

enum class MaterialStagingRejectionReason {
    private_material_limit_exceeded,
    storage_reserve_unavailable,
};

enum class MaterialStagingCheckError {
    invalid_run_id,
    invalid_inventory,
    duplicate_material,
    capacity_exceeded,
};

struct MaterialStagingPermit {
    std::uint64_t selected_bytes{};
    std::uint64_t already_staged_bytes{};
    std::uint64_t bytes_to_allocate{};
    std::uint64_t required_free_bytes{};
    bool operator==(const MaterialStagingPermit &) const = default;
};

struct MaterialStagingRejection {
    MaterialStagingRejectionReason reason{
        MaterialStagingRejectionReason::private_material_limit_exceeded};
    EvidenceResult evidence_result{EvidenceResult::incomplete};
    Diagnostic diagnostic;
    std::uint64_t selected_bytes{};
    std::uint64_t bytes_to_allocate{};
    std::uint64_t required_free_bytes{};
    bool operator==(const MaterialStagingRejection &) const = default;
};

using MaterialStagingCheckResult =
    std::variant<MaterialStagingPermit, MaterialStagingRejection, MaterialStagingCheckError>;

[[nodiscard]] MaterialStagingCheckResult
check_material_staging_budget(std::string_view run_id,
                              std::span<const MaterialStagingItem> inventory,
                              std::uint64_t available_bytes) noexcept;

[[nodiscard]] std::string_view
material_staging_rejection_code(MaterialStagingRejectionReason reason) noexcept;

} // namespace ayther::audio_qa
