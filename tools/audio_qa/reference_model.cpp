#include "reference_model.h"
#include "model_limits.h"

#include <string_view>

namespace ayther::audio_qa {
namespace {
bool identifier(std::string_view value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}
bool attributed(const Provenance &source) noexcept {
    return source.origin > Origin::unknown && source.origin <= Origin::runtime_observation &&
           identifier(source.evidence_id);
}
template <class T> bool valid(const Recorded<T> &field) noexcept {
    // Missing data may still carry evidence explaining its unavailability.
    return (!field.value && field.provenance.origin == Origin::unknown &&
            field.provenance.evidence_id.empty()) ||
           attributed(field.provenance);
}
bool valid(const Recorded<std::string> &field) noexcept {
    return valid<std::string>(field) &&
           (!field.value || field.value->size() <= max_reference_value_bytes);
}
bool valid(const BuildReference &build) noexcept {
    return valid(build.artifact) && valid(build.release) && valid(build.commit) &&
           valid(build.variant) && valid(build.abi);
}
} // namespace

bool well_formed(const Material &material) noexcept {
    if (!identifier(material.material_id) || material.role < MaterialRole::rom ||
        material.role > MaterialRole::runtime_build || !valid(material.source_locator) ||
        !valid(material.format_version) || !valid(material.identified) ||
        !valid(material.consumed)) {
        return false;
    }
    switch (material.stability) {
    case Stability::unverified:
        return (material.stability_provenance.origin == Origin::unknown &&
                material.stability_provenance.evidence_id.empty()) ||
               attributed(material.stability_provenance);
    case Stability::verified:
        return attributed(material.stability_provenance) && material.identified.value &&
               material.consumed.value && material.identified.value == material.consumed.value;
    case Stability::changed:
        return attributed(material.stability_provenance) && material.identified.value &&
               material.consumed.value && material.identified.value != material.consumed.value;
    }
    return false;
}

bool well_formed(const Reference &reference) noexcept {
    if (!identifier(reference.baseline_id) || !identifier(reference.execution_reference_id) ||
        reference.role < ReferenceRole::initial || reference.role > ReferenceRole::extended ||
        reference.materials.size() > max_reference_materials ||
        reference.conditions.size() > max_reference_conditions ||
        reference.declared_differences.size() > max_reference_differences ||
        !valid(reference.engine) || !valid(reference.runtime) ||
        !valid(reference.conditions_manifest_id)) {
        return false;
    }
    for (const auto &material : reference.materials) {
        if (!well_formed(material)) {
            return false;
        }
    }
    for (const auto &condition : reference.conditions) {
        if (!identifier(condition.key) || !valid(condition.value)) {
            return false;
        }
    }
    for (const auto &difference : reference.declared_differences) {
        if (!valid(difference)) {
            return false;
        }
    }
    return true;
}

} // namespace ayther::audio_qa
