#include "material_consumption_guard.h"

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

qa::ContentIdentity identity(const std::uint8_t marker, const std::uint64_t size) {
    qa::ContentIdentity value;
    value.sha256[0] = marker;
    value.byte_size = size;
    return value;
}

qa::Reference initial_reference(const qa::ContentIdentity &expected) {
    qa::Material pack;
    pack.material_id = "golden-axe-pack";
    pack.role = qa::MaterialRole::pack;
    pack.identified = {expected, {qa::Origin::content_measurement, "initial-pack-hash"}};

    qa::Reference reference;
    reference.baseline_id = "play-ce-initial";
    reference.execution_reference_id = "play-ce-initial";
    reference.materials.push_back(pack);
    return reference;
}

qa::MaterialConsumptionObservation observation(const std::optional<qa::ContentIdentity> &observed) {
    qa::MaterialConsumptionObservation value;
    value.stage = "pack_staging";
    value.observed_identity = {observed, {qa::Origin::content_measurement, "pack-copy-hash"}};
    value.affected_samples = {
        qa::Availability::known, qa::SampleFrameRange{"engine_main_output", 48000, 1920, 2400}, {}};
    value.last_confirmed_frame = {qa::Availability::known, 119, {}};
    return value;
}

} // namespace

int main() {
    try {
        const auto expected = identity(0x11, 4096);
        const auto changed = identity(0x22, 4096);
        const auto initial = initial_reference(expected);

        const auto rejected =
            qa::check_material_consumption(initial, "golden-axe-pack", observation(changed));
        const auto *blocked = std::get_if<qa::MaterialConsumptionBlocked>(&rejected);
        require(blocked != nullptr, "changed_material_was_allowed");
        require(blocked->reason == qa::MaterialConsumptionBlockReason::identity_changed &&
                    blocked->evidence_result == qa::EvidenceResult::incomplete &&
                    blocked->observed_material.identified.value == expected &&
                    blocked->observed_material.consumed.value == changed &&
                    blocked->observed_material.stability == qa::Stability::changed,
                "changed_identity_was_not_preserved_separately");
        require(blocked->diagnostic.code == "material_identity_changed" &&
                    blocked->diagnostic.scope == qa::DiagnosticScope::material &&
                    blocked->diagnostic.severity == qa::DiagnosticSeverity::error &&
                    blocked->diagnostic.subject_id == "golden-axe-pack" &&
                    blocked->diagnostic.stage == "pack_staging" &&
                    blocked->diagnostic.affected_samples.value ==
                        qa::SampleFrameRange{"engine_main_output", 48000, 1920, 2400} &&
                    blocked->diagnostic.last_confirmed_frame.value == 119 &&
                    qa::well_formed(blocked->diagnostic),
                "changed_material_diagnostic_lost_context");
        require(initial == initial_reference(expected) &&
                    initial.materials.front().consumed.value == std::nullopt &&
                    initial.materials.front().stability == qa::Stability::unverified,
                "initial_reference_was_rewritten_with_changed_bytes");

        std::size_t consumed{};
        if (std::holds_alternative<qa::MaterialConsumptionPermit>(rejected)) {
            ++consumed;
        }
        require(consumed == 0, "blocked_material_reached_consumer");

        const auto unavailable =
            qa::check_material_consumption(initial, "golden-axe-pack", observation(std::nullopt));
        const auto *unverified = std::get_if<qa::MaterialConsumptionBlocked>(&unavailable);
        require(unverified != nullptr &&
                    unverified->reason ==
                        qa::MaterialConsumptionBlockReason::identity_unavailable &&
                    unverified->diagnostic.code == "material_identity_unverified" &&
                    !unverified->observed_material.consumed.value,
                "unverified_material_was_not_blocked");

        const auto accepted =
            qa::check_material_consumption(initial, "golden-axe-pack", observation(expected));
        const auto *permit = std::get_if<qa::MaterialConsumptionPermit>(&accepted);
        require(permit != nullptr && permit->material_id == "golden-axe-pack" &&
                    permit->verified_identity == expected &&
                    permit->observed_material.stability == qa::Stability::verified &&
                    permit->observed_material.consumed.value == expected,
                "matching_material_was_not_permitted");
        if (permit != nullptr) {
            ++consumed;
        }
        require(consumed == 1, "consumer_did_not_require_a_verified_permit");

        auto invalid = observation(changed);
        invalid.stage.clear();
        const auto invalid_result =
            qa::check_material_consumption(initial, "golden-axe-pack", invalid);
        require(std::get_if<qa::MaterialConsumptionCheckError>(&invalid_result) != nullptr,
                "invalid_observation_was_not_rejected");

        std::puts("material_consumption_guard_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "material_consumption_guard_test: %s\n", error.what());
        return 1;
    }
}
