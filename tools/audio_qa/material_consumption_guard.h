#pragma once

#include "coverage_model.h"
#include "model.h"
#include "reference_model.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace ayther::audio_qa {

struct MaterialConsumptionObservation {
    std::string stage;
    Recorded<ContentIdentity> observed_identity;
    Field<SampleFrameRange> affected_samples;
    Field<std::uint64_t> last_confirmed_frame;
    bool operator==(const MaterialConsumptionObservation &) const = default;
};

struct MaterialConsumptionPermit {
    std::string material_id;
    ContentIdentity verified_identity;
    Material observed_material;
    bool operator==(const MaterialConsumptionPermit &) const = default;
};

enum class MaterialConsumptionBlockReason {
    identity_unavailable,
    identity_changed,
};

struct MaterialConsumptionBlocked {
    MaterialConsumptionBlockReason reason{MaterialConsumptionBlockReason::identity_unavailable};
    EvidenceResult evidence_result{EvidenceResult::incomplete};
    Material observed_material;
    Diagnostic diagnostic;
    bool operator==(const MaterialConsumptionBlocked &) const = default;
};

enum class MaterialConsumptionCheckError {
    invalid_initial_reference,
    material_not_found,
    ambiguous_material,
    invalid_observation,
};

using MaterialConsumptionCheckResult =
    std::variant<MaterialConsumptionPermit, MaterialConsumptionBlocked,
                 MaterialConsumptionCheckError>;

[[nodiscard]] MaterialConsumptionCheckResult
check_material_consumption(const Reference &initial_reference, std::string_view material_id,
                           const MaterialConsumptionObservation &observation) noexcept;

} // namespace ayther::audio_qa
