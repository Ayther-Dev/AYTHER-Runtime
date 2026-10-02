#include "runtime_reference_comparison.h"

#include <cstdio>
#include <stdexcept>
#include <variant>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

qa::ContentIdentity content(const std::uint8_t marker) {
    qa::ContentIdentity identity;
    identity.sha256[0] = marker;
    identity.byte_size = 4096;
    return identity;
}

qa::LockedRuntimeExecutable locked_runtime() {
    qa::LockedRuntimeExecutable locked;
    locked.resolved_locator = {"C:/play/runtime-lock/bin/ayther_runtime.exe",
                               {qa::Origin::artifact_manifest, "play-runtime-lock"}};
    locked.build.artifact = {content(0x42), {qa::Origin::content_measurement, "locked-exe-hash"}};
    locked.build.release = {"v0.1.0-beta.6", {qa::Origin::artifact_manifest, "play-runtime-lock"}};
    locked.build.commit = {"cbe90d2cd1d443dc53bbc1d5fa0234197b2982a3",
                           {qa::Origin::artifact_manifest, "play-runtime-lock"}};
    return locked;
}

qa::Reference initial_reference() {
    qa::Reference reference;
    reference.baseline_id = "play-beta-6";
    reference.execution_reference_id = "play-launch-128";
    return reference;
}

const qa::RuntimeReferenceComparison &comparison(const qa::RuntimeReferenceComparisonResult &result,
                                                 const char *const message) {
    const auto *value = std::get_if<qa::RuntimeReferenceComparison>(&result);
    require(value != nullptr, message);
    return *value;
}

} // namespace

int main() {
    try {
        const auto locked = locked_runtime();
        qa::ConsumedRuntimeExecutable configured_elsewhere;
        configured_elsewhere.configured_locator = {
            "C:/custom/runtime/ayther_runtime.exe",
            {qa::Origin::configuration, "play-runtime-path"}};
        configured_elsewhere.artifact = {content(0x42),
                                         {qa::Origin::content_measurement, "configured-hash"}};

        const auto different_path = comparison(
            qa::compare_runtime_reference(initial_reference(), locked, configured_elsewhere,
                                          "effective-custom-runtime",
                                          {qa::Origin::content_measurement, "runtime-comparison"}),
            "different_configured_path_was_rejected");
        require(different_path.match == qa::RuntimeReferenceMatch::different_locator &&
                    different_path.initial.role == qa::ReferenceRole::initial &&
                    different_path.initial.execution_reference_id == "play-launch-128" &&
                    different_path.effective.role == qa::ReferenceRole::extended &&
                    different_path.effective.execution_reference_id == "effective-custom-runtime" &&
                    !different_path.effective.runtime.release.value &&
                    !different_path.effective.runtime.commit.value &&
                    different_path.effective.runtime.artifact == configured_elsewhere.artifact &&
                    different_path.effective.declared_differences.size() == 1 &&
                    different_path.consumed_executable.source_locator ==
                        configured_elsewhere.configured_locator &&
                    different_path.consumed_executable.stability == qa::Stability::verified,
                "different_path_was_labeled_as_locked_release");

        qa::ConsumedRuntimeExecutable matching;
        matching.configured_locator = {*locked.resolved_locator.value,
                                       {qa::Origin::configuration, "play-runtime-path"}};
        matching.artifact = {*locked.build.artifact.value,
                             {qa::Origin::content_measurement, "configured-hash"}};
        const auto same =
            comparison(qa::compare_runtime_reference(
                           initial_reference(), locked, matching, "effective-locked-runtime",
                           {qa::Origin::content_measurement, "runtime-comparison"}),
                       "matching_runtime_was_rejected");
        require(same.match == qa::RuntimeReferenceMatch::verified_same &&
                    same.effective.role == qa::ReferenceRole::initial &&
                    same.effective.runtime.release.value == "v0.1.0-beta.6" &&
                    same.effective.runtime.commit.value ==
                        "cbe90d2cd1d443dc53bbc1d5fa0234197b2982a3" &&
                    same.effective.declared_differences.empty(),
                "matching_runtime_did_not_inherit_locked_identity");

        auto changed = matching;
        changed.artifact = {content(0x77), {qa::Origin::content_measurement, "changed-hash"}};
        const auto different_content =
            comparison(qa::compare_runtime_reference(
                           initial_reference(), locked, changed, "effective-changed-runtime",
                           {qa::Origin::content_measurement, "runtime-comparison"}),
                       "changed_runtime_was_rejected");
        require(different_content.match == qa::RuntimeReferenceMatch::different_content &&
                    different_content.effective.role == qa::ReferenceRole::extended &&
                    !different_content.effective.runtime.release.value &&
                    different_content.consumed_executable.stability == qa::Stability::changed,
                "changed_runtime_was_labeled_as_locked_release");

        auto unmeasured = matching;
        unmeasured.artifact = {};
        const auto unverified =
            comparison(qa::compare_runtime_reference(
                           initial_reference(), locked, unmeasured, "effective-unverified-runtime",
                           {qa::Origin::runtime_observation, "runtime-comparison"}),
                       "unmeasured_runtime_was_rejected");
        require(unverified.match == qa::RuntimeReferenceMatch::unverified &&
                    unverified.effective.role == qa::ReferenceRole::extended &&
                    !unverified.effective.runtime.release.value &&
                    unverified.consumed_executable.stability == qa::Stability::unverified,
                "unmeasured_runtime_was_labeled_as_locked_release");

        std::puts("runtime_reference_comparison_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "runtime_reference_comparison_test: %s\n", error.what());
        return 1;
    }
}
