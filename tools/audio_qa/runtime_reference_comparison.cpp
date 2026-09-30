#include "runtime_reference_comparison.h"

#include <utility>

namespace ayther::audio_qa {
namespace {

constexpr auto runtime_material_id = "runtime-executable";

bool valid_locked(const LockedRuntimeExecutable &locked) {
    Reference probe;
    probe.baseline_id = "locked-runtime-probe";
    probe.execution_reference_id = "locked-runtime-probe";
    probe.runtime = locked.build;
    Material material;
    material.material_id = runtime_material_id;
    material.role = MaterialRole::runtime_build;
    material.source_locator = locked.resolved_locator;
    probe.materials.push_back(std::move(material));
    return locked.resolved_locator.value.has_value() && !locked.resolved_locator.value->empty() &&
           well_formed(probe);
}

bool valid_consumed(const ConsumedRuntimeExecutable &consumed) {
    Reference probe;
    probe.baseline_id = "consumed-runtime-probe";
    probe.execution_reference_id = "consumed-runtime-probe";
    probe.runtime.artifact = consumed.artifact;
    Material material;
    material.material_id = runtime_material_id;
    material.role = MaterialRole::runtime_build;
    material.source_locator = consumed.configured_locator;
    material.consumed = consumed.artifact;
    probe.materials.push_back(std::move(material));
    return consumed.configured_locator.value.has_value() &&
           !consumed.configured_locator.value->empty() && well_formed(probe);
}

RuntimeReferenceMatch classify(const LockedRuntimeExecutable &locked,
                               const ConsumedRuntimeExecutable &consumed) {
    if (locked.resolved_locator.value != consumed.configured_locator.value) {
        return RuntimeReferenceMatch::different_locator;
    }
    if (!locked.build.artifact.value || !consumed.artifact.value) {
        return RuntimeReferenceMatch::unverified;
    }
    return locked.build.artifact.value == consumed.artifact.value
               ? RuntimeReferenceMatch::verified_same
               : RuntimeReferenceMatch::different_content;
}

std::string difference_for(const RuntimeReferenceMatch match) {
    switch (match) {
    case RuntimeReferenceMatch::different_locator:
        return "runtime executable locator differs from locked selection";
    case RuntimeReferenceMatch::different_content:
        return "runtime executable content differs from locked selection";
    case RuntimeReferenceMatch::unverified:
        return "runtime executable equivalence could not be verified";
    case RuntimeReferenceMatch::verified_same:
        return {};
    }
    return "runtime executable comparison is invalid";
}

} // namespace

RuntimeReferenceComparisonResult
compare_runtime_reference(Reference initial, const LockedRuntimeExecutable &locked,
                          const ConsumedRuntimeExecutable &consumed,
                          std::string execution_reference_id, Provenance comparison_provenance) {
    if (!well_formed(initial)) {
        return RuntimeReferenceComparisonError::invalid_initial_reference;
    }
    if (!valid_locked(locked)) {
        return RuntimeReferenceComparisonError::invalid_locked_executable;
    }
    if (!valid_consumed(consumed)) {
        return RuntimeReferenceComparisonError::invalid_consumed_executable;
    }

    RuntimeReferenceComparison comparison;
    comparison.initial = initial;
    comparison.match = classify(locked, consumed);
    comparison.effective = std::move(initial);
    comparison.effective.execution_reference_id = std::move(execution_reference_id);

    comparison.consumed_executable.material_id = runtime_material_id;
    comparison.consumed_executable.role = MaterialRole::runtime_build;
    comparison.consumed_executable.source_locator = consumed.configured_locator;
    comparison.consumed_executable.identified = locked.build.artifact;
    comparison.consumed_executable.consumed = consumed.artifact;
    comparison.consumed_executable.stability_provenance = comparison_provenance;
    if (locked.build.artifact.value && consumed.artifact.value) {
        comparison.consumed_executable.stability =
            locked.build.artifact.value == consumed.artifact.value ? Stability::verified
                                                                   : Stability::changed;
    }

    if (comparison.match == RuntimeReferenceMatch::verified_same) {
        comparison.effective.role = ReferenceRole::initial;
        comparison.effective.runtime = locked.build;
    } else {
        comparison.effective.role = ReferenceRole::extended;
        comparison.effective.runtime = {};
        comparison.effective.runtime.artifact = consumed.artifact;
        comparison.effective.declared_differences.push_back(
            {difference_for(comparison.match), comparison_provenance});
    }
    comparison.effective.materials.push_back(comparison.consumed_executable);

    if (!well_formed(comparison.consumed_executable) || !well_formed(comparison.effective)) {
        return RuntimeReferenceComparisonError::invalid_effective_reference;
    }
    return comparison;
}

} // namespace ayther::audio_qa
