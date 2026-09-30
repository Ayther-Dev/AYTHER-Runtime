#include "model_limits.h"
#include "reference_model.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        qa::ContentIdentity content;
        content.sha256[0] = 42;
        content.byte_size = 4096;
        qa::Material material;
        material.material_id = "pack-1";
        material.role = qa::MaterialRole::pack;
        material.identified = {content, {qa::Origin::content_measurement, "hash-before"}};
        require(qa::well_formed(material) && !material.consumed.value,
                "identification_implied_consumption");
        material.stability = qa::Stability::verified;
        material.stability_provenance = {qa::Origin::runtime_observation, "fixed-handle"};
        require(!qa::well_formed(material), "missing_consumed_identity_accepted_as_verified");
        material.consumed = {content, {qa::Origin::runtime_observation, "consumed-hash"}};
        require(qa::well_formed(material), "matching_consumed_identity_rejected");
        material.consumed.value->sha256[1] = 7;
        require(!qa::well_formed(material), "changed_content_accepted_as_stable");
        material.stability = qa::Stability::changed;
        require(qa::well_formed(material), "changed_content_diagnostic_lost");

        qa::Reference initial;
        initial.baseline_id = "baseline-1";
        initial.execution_reference_id = "reference-1";
        initial.engine.release = {"rc.9", {qa::Origin::artifact_manifest, "release-manifest"}};
        require(qa::well_formed(initial) && !initial.engine.artifact.value,
                "release_name_implied_artifact_identity");
        auto extended = initial;
        extended.role = qa::ReferenceRole::extended;
        extended.execution_reference_id = "reference-2";
        extended.materials.push_back(material);
        extended.conditions.push_back(
            {"profile", {"full", {qa::Origin::configuration, "play-config"}}});
        require(qa::well_formed(extended) && initial.materials.empty() &&
                    initial.execution_reference_id == "reference-1",
                "extended_reference_replaced_initial");
        extended.runtime.commit.value = "unattributed";
        require(!qa::well_formed(extended), "value_without_provenance_accepted");
        extended.runtime.commit = {};
        extended.runtime.commit.provenance = {qa::Origin::runtime_observation,
                                              "commit-unavailable"};
        require(qa::well_formed(extended), "unavailable_field_cannot_keep_diagnostic_provenance");
        extended.runtime.commit.provenance.origin = static_cast<qa::Origin>(-1);
        require(!qa::well_formed(extended), "invalid_provenance_origin_accepted");
        extended.runtime.commit = {};
        extended.materials.assign(qa::max_reference_materials, material);
        extended.conditions.assign(qa::max_reference_conditions,
                                   {"profile", {"full", {qa::Origin::configuration, "config"}}});
        extended.declared_differences.assign(
            qa::max_reference_differences, {std::string(qa::max_reference_value_bytes, 'd'),
                                            {qa::Origin::user_declaration, "declared-difference"}});
        require(qa::well_formed(extended), "reference_resource_boundary_rejected");
        extended.materials.push_back(material);
        require(!qa::well_formed(extended), "reference_material_limit_ignored");
        extended.materials.pop_back();
        extended.conditions.push_back({"extra", {}});
        require(!qa::well_formed(extended), "reference_condition_limit_ignored");
        extended.conditions.pop_back();
        extended.declared_differences.push_back({});
        require(!qa::well_formed(extended), "reference_difference_limit_ignored");
        extended.declared_differences.pop_back();
        extended.declared_differences[0].value->push_back('x');
        require(!qa::well_formed(extended), "reference_value_limit_ignored");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "qa_reference_model_test_failed: %s\n", error.what());
        return 1;
    }
}
