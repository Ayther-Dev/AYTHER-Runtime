#include "play_launch_manifest.h"

#include "model_limits.h"

#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <toml++/toml.hpp>
#include <utility>

namespace ayther::audio_qa {
namespace {

bool identifier(const std::string_view value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}

std::optional<std::string> string_field(const toml::table &table, const std::string_view key,
                                        bool &valid) {
    const auto node = table[key];
    if (!node) {
        return {};
    }
    const auto value = node.value<std::string>();
    if (!value.has_value() || value->size() > max_reference_value_bytes) {
        valid = false;
        return {};
    }
    return value;
}

std::optional<std::uint64_t> unsigned_field(const toml::table &table, const std::string_view key,
                                            bool &valid) {
    const auto node = table[key];
    if (!node) {
        return {};
    }
    const auto value = node.value<std::int64_t>();
    if (!value.has_value() || *value < 0) {
        valid = false;
        return {};
    }
    return static_cast<std::uint64_t>(*value);
}

std::optional<bool> boolean_field(const toml::table &table, const std::string_view key,
                                  bool &valid) {
    const auto node = table[key];
    if (!node) {
        return {};
    }
    const auto value = node.value<bool>();
    if (!value.has_value()) {
        valid = false;
        return {};
    }
    return value;
}

void append_condition(Reference &reference, std::string key, std::optional<std::string> value,
                      const Provenance &provenance) {
    if (value.has_value()) {
        reference.conditions.push_back({std::move(key), {std::move(value), provenance}});
    }
}

void append_material(Reference &reference, std::string material_id, const MaterialRole role,
                     std::optional<std::string> locator, const Provenance &provenance) {
    if (!locator.has_value() || locator->empty()) {
        return;
    }
    Material material;
    material.material_id = std::move(material_id);
    material.role = role;
    material.source_locator = {std::move(locator), provenance};
    reference.materials.push_back(std::move(material));
}

PlayLaunchManifestResult decode(const std::string_view text, std::string baseline_id,
                                std::string execution_reference_id, std::string evidence_id) {
    if (!identifier(baseline_id) || !identifier(execution_reference_id) ||
        !identifier(evidence_id)) {
        return PlayLaunchManifestError::invalid_identity;
    }
    if (text.size() > max_play_launch_manifest_bytes) {
        return PlayLaunchManifestError::input_too_large;
    }

    try {
        const auto table = toml::parse(text);
        bool valid = true;
        const auto format = unsigned_field(table, "format", valid);
        if (!valid || (format.has_value() && *format > supported_play_launch_manifest_format)) {
            return valid ? PlayLaunchManifestError::unsupported_format
                         : PlayLaunchManifestError::invalid_manifest;
        }

        const auto session_id = string_field(table, "id", valid);
        const auto created = string_field(table, "created", valid);
        const auto play_version = string_field(table, "play_version", valid);
        const auto rom = string_field(table, "rom", valid);
        const auto rom_crc32 = string_field(table, "rom_crc32", valid);
        const auto core = string_field(table, "core", valid);
        const auto pack = string_field(table, "pack", valid);
        const auto profile = string_field(table, "profile", valid);
        const auto subsystems = unsigned_field(table, "subsystems", valid);
        const auto muted_buses = unsigned_field(table, "muted_buses", valid);
        const auto shaders = boolean_field(table, "shaders", valid);
        const auto output = string_field(table, "output", valid);
        const auto no_flicker = boolean_field(table, "no_flicker", valid);
        const auto patch = string_field(table, "patch", valid);
        const auto save = string_field(table, "save", valid);

        if (!valid || !session_id.has_value() || session_id->empty() || !created.has_value() ||
            created->empty() || !play_version.has_value() || play_version->empty() ||
            !rom.has_value() || rom->empty() || !core.has_value() || core->empty() ||
            (subsystems.has_value() && *subsystems > std::numeric_limits<std::uint32_t>::max()) ||
            (muted_buses.has_value() && *muted_buses > std::numeric_limits<std::uint32_t>::max())) {
            return PlayLaunchManifestError::invalid_manifest;
        }

        const Provenance provenance{Origin::artifact_manifest, evidence_id};
        Reference reference;
        reference.baseline_id = std::move(baseline_id);
        reference.execution_reference_id = std::move(execution_reference_id);
        reference.role = ReferenceRole::initial;
        reference.conditions_manifest_id = {*session_id, provenance};

        append_material(reference, "rom", MaterialRole::rom, rom, provenance);
        append_material(reference, "core", MaterialRole::core, core, provenance);
        append_material(reference, "pack", MaterialRole::pack, pack, provenance);

        append_condition(reference, "play_manifest.format",
                         format.has_value() ? std::optional{std::to_string(*format)} : std::nullopt,
                         provenance);
        append_condition(reference, "play_manifest.created", created, provenance);
        append_condition(reference, "play.version", play_version, provenance);
        append_condition(reference, "rom.crc32", rom_crc32, provenance);
        append_condition(reference, "profile", profile, provenance);
        append_condition(reference, "subsystems",
                         subsystems.has_value() ? std::optional{std::to_string(*subsystems)}
                                                : std::nullopt,
                         provenance);
        append_condition(reference, "muted_buses",
                         muted_buses.has_value() ? std::optional{std::to_string(*muted_buses)}
                                                 : std::nullopt,
                         provenance);
        append_condition(reference, "shaders",
                         shaders.has_value()
                             ? std::optional<std::string>{*shaders ? "true" : "false"}
                             : std::nullopt,
                         provenance);
        append_condition(reference, "output", output, provenance);
        append_condition(reference, "no_flicker",
                         no_flicker.has_value()
                             ? std::optional<std::string>{*no_flicker ? "true" : "false"}
                             : std::nullopt,
                         provenance);
        append_condition(reference, "patch", patch, provenance);
        append_condition(reference, "save", save, provenance);

        return well_formed(reference)
                   ? PlayLaunchManifestResult{std::move(reference)}
                   : PlayLaunchManifestResult{PlayLaunchManifestError::invalid_manifest};
    } catch (const toml::parse_error &) {
        return PlayLaunchManifestError::parse_error;
    }
}

} // namespace

PlayLaunchManifestResult decode_play_launch_manifest(const std::string_view text,
                                                     std::string baseline_id,
                                                     std::string execution_reference_id,
                                                     std::string evidence_id) {
    return decode(text, std::move(baseline_id), std::move(execution_reference_id),
                  std::move(evidence_id));
}

PlayLaunchManifestResult load_play_launch_manifest(const std::filesystem::path &path,
                                                   std::string baseline_id,
                                                   std::string execution_reference_id,
                                                   std::string evidence_id) noexcept {
    try {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) {
            return PlayLaunchManifestError::io_error;
        }
        if (size > max_play_launch_manifest_bytes) {
            return PlayLaunchManifestError::input_too_large;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return PlayLaunchManifestError::io_error;
        }
        const std::string text{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
        if (input.bad() || text.size() != size) {
            return PlayLaunchManifestError::io_error;
        }
        return decode(text, std::move(baseline_id), std::move(execution_reference_id),
                      std::move(evidence_id));
    } catch (...) {
        return PlayLaunchManifestError::io_error;
    }
}

} // namespace ayther::audio_qa
