#include "play_launch_manifest.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const qa::EffectiveCondition *condition(const qa::Reference &reference,
                                        const std::string_view key) {
    for (const auto &item : reference.conditions) {
        if (item.key == key) {
            return &item;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    try {
        constexpr std::string_view manifest = R"(format = 1
id = "session-128"
created = "2026-09-27T17:13:48Z"
play_version = "0.1.0"
rom = "C:/private/game.rom"
rom_crc32 = "665D7DF9"
core = "C:/cores/core.dll"
pack = "C:/packs/pack.ay"
profile = "full"
muted_buses = 0
shaders = true
output = "lcd"
no_flicker = true
patch = ""
save = ""
)";
        const auto path = std::filesystem::current_path() / "qa-128-play-launch-manifest.toml";
        (void)std::filesystem::remove(path);
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            require(static_cast<bool>(output), "fixture_open_failed");
            output.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
            require(static_cast<bool>(output), "fixture_write_failed");
        }

        const auto loaded = qa::load_play_launch_manifest(path, "baseline-initial",
                                                          "reference-initial", "play-launch-128");
        (void)std::filesystem::remove(path);
        const auto *reference = std::get_if<qa::Reference>(&loaded);
        require(reference != nullptr && qa::well_formed(*reference) &&
                    reference->role == qa::ReferenceRole::initial &&
                    reference->conditions_manifest_id.value == "session-128" &&
                    reference->materials.size() == 3,
                "initial_reference_was_not_imported");

        for (const auto &item : reference->conditions) {
            require(item.value.provenance.origin == qa::Origin::artifact_manifest &&
                        item.value.provenance.evidence_id == "play-launch-128",
                    "condition_lost_manifest_provenance");
        }
        const auto *profile = condition(*reference, "profile");
        const auto *muted_buses = condition(*reference, "muted_buses");
        const auto *patch = condition(*reference, "patch");
        const auto *save = condition(*reference, "save");
        require(profile != nullptr && profile->value.value == "full" && muted_buses != nullptr &&
                    muted_buses->value.value == "0" && patch != nullptr &&
                    patch->value.value == "" && save != nullptr && save->value.value == "" &&
                    condition(*reference, "subsystems") == nullptr,
                "missing_condition_was_defaulted_or_explicit_value_was_lost");

        constexpr std::string_view minimal = R"(id = "session-minimal"
created = "2026-09-27T17:13:48Z"
play_version = "0.1.0"
rom = "C:/private/game.rom"
core = "C:/cores/core.dll"
)";
        const auto decoded = qa::decode_play_launch_manifest(minimal, "baseline-minimal",
                                                             "reference-minimal", "play-minimal");
        const auto *minimal_reference = std::get_if<qa::Reference>(&decoded);
        require(minimal_reference != nullptr && minimal_reference->materials.size() == 2 &&
                    condition(*minimal_reference, "play_manifest.format") == nullptr &&
                    condition(*minimal_reference, "profile") == nullptr &&
                    condition(*minimal_reference, "muted_buses") == nullptr &&
                    condition(*minimal_reference, "shaders") == nullptr,
                "absent_manifest_fields_became_explicit_defaults");

        const auto unsupported = qa::decode_play_launch_manifest(
            "format = 2\nid = \"x\"\n", "baseline", "reference", "manifest");
        require(std::get_if<qa::PlayLaunchManifestError>(&unsupported) != nullptr &&
                    std::get<qa::PlayLaunchManifestError>(unsupported) ==
                        qa::PlayLaunchManifestError::unsupported_format,
                "unsupported_manifest_format_was_accepted");

        std::puts("play_launch_manifest_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "play_launch_manifest_test: %s\n", error.what());
        return 1;
    }
}
