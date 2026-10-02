#include "material_consumption_guard.h"

#include "model_limits.h"

#include <utility>

namespace ayther::audio_qa {
namespace {

bool identifier(const std::string_view value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}

bool valid_observation(const MaterialConsumptionObservation &observation) noexcept {
    return identifier(observation.stage) && consistent_availability(observation.affected_samples) &&
           (!observation.affected_samples.value ||
            well_formed(*observation.affected_samples.value)) &&
           consistent_availability(observation.last_confirmed_frame);
}

Diagnostic blocked_diagnostic(const Material &material,
                              const MaterialConsumptionObservation &observation,
                              const MaterialConsumptionBlockReason reason) {
    Diagnostic diagnostic;
    diagnostic.code = reason == MaterialConsumptionBlockReason::identity_changed
                          ? "material_identity_changed"
                          : "material_identity_unverified";
    diagnostic.scope = DiagnosticScope::material;
    diagnostic.severity = DiagnosticSeverity::error;
    diagnostic.subject_id = material.material_id;
    diagnostic.stage = observation.stage;
    diagnostic.affected_samples = observation.affected_samples;
    diagnostic.last_confirmed_frame = observation.last_confirmed_frame;
    diagnostic.detail = reason == MaterialConsumptionBlockReason::identity_changed
                            ? "observed content identity differs from the initial reference; "
                              "consumption was blocked"
                            : "content identity could not be verified against the initial "
                              "reference; consumption was blocked";
    return diagnostic;
}

} // namespace

MaterialConsumptionCheckResult
check_material_consumption(const Reference &initial_reference, const std::string_view material_id,
                           const MaterialConsumptionObservation &observation) noexcept {
    try {
        if (!well_formed(initial_reference) || initial_reference.role != ReferenceRole::initial ||
            !identifier(material_id)) {
            return MaterialConsumptionCheckError::invalid_initial_reference;
        }
        if (!valid_observation(observation)) {
            return MaterialConsumptionCheckError::invalid_observation;
        }

        const Material *initial_material{};
        for (const auto &material : initial_reference.materials) {
            if (material.material_id != material_id) {
                continue;
            }
            if (initial_material != nullptr) {
                return MaterialConsumptionCheckError::ambiguous_material;
            }
            initial_material = &material;
        }
        if (initial_material == nullptr) {
            return MaterialConsumptionCheckError::material_not_found;
        }
        if (!initial_material->identified.value) {
            return MaterialConsumptionCheckError::invalid_initial_reference;
        }

        auto observed_material = *initial_material;
        observed_material.consumed = observation.observed_identity;
        observed_material.stability_provenance = observation.observed_identity.provenance;

        if (!observation.observed_identity.value) {
            observed_material.stability = Stability::unverified;
            const auto diagnostic =
                blocked_diagnostic(observed_material, observation,
                                   MaterialConsumptionBlockReason::identity_unavailable);
            if (!well_formed(observed_material) || !well_formed(diagnostic)) {
                return MaterialConsumptionCheckError::invalid_observation;
            }
            return MaterialConsumptionBlocked{MaterialConsumptionBlockReason::identity_unavailable,
                                              EvidenceResult::incomplete,
                                              std::move(observed_material), diagnostic};
        }

        if (*observation.observed_identity.value != *initial_material->identified.value) {
            observed_material.stability = Stability::changed;
            const auto diagnostic = blocked_diagnostic(
                observed_material, observation, MaterialConsumptionBlockReason::identity_changed);
            if (!well_formed(observed_material) || !well_formed(diagnostic)) {
                return MaterialConsumptionCheckError::invalid_observation;
            }
            return MaterialConsumptionBlocked{MaterialConsumptionBlockReason::identity_changed,
                                              EvidenceResult::incomplete,
                                              std::move(observed_material), diagnostic};
        }

        observed_material.stability = Stability::verified;
        if (!well_formed(observed_material)) {
            return MaterialConsumptionCheckError::invalid_observation;
        }
        return MaterialConsumptionPermit{initial_material->material_id,
                                         *observation.observed_identity.value,
                                         std::move(observed_material)};
    } catch (...) {
        return MaterialConsumptionCheckError::invalid_observation;
    }
}

} // namespace ayther::audio_qa
