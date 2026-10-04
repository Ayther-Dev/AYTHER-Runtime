#pragma once

#include "check_options.h"
#include "field_issue.h"
#include "reference_model.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

// Spec 002 (RF-1.2, RF-1.6, plan §5.1): every value the replay uses, with the
// place it came from. Precedence: explicit option > Play manifest > reference >
// environment > default. ROM, takes and pack only accept explicit options.
enum class ValueSource {
    explicit_option,
    play_manifest,
    reference,
    environment,
    default_value,
    generated,
};

struct EffectiveValue {
    std::string key;
    std::string value;
    ValueSource source{ValueSource::default_value};
    bool operator==(const EffectiveValue &) const = default;
};

struct EffectiveConditions {
    std::optional<std::string> profile;
    std::optional<std::string> subsystems;
    std::optional<std::string> mute_buses;
    std::optional<std::string> video_output;
    std::optional<std::string> patch;
    std::optional<bool> shaders;
    std::vector<std::string> core_options; // key=value, in effective order
    bool operator==(const EffectiveConditions &) const = default;
};

struct EffectiveRequest {
    std::string runtime;
    std::string rom;
    std::string core;
    std::vector<std::string> takes;
    std::optional<std::string> pack; // nullopt: «Sin pack» (RF-1.3)
    std::string pack_mode;
    std::string trust_registry;
    std::string output;
    std::string request_id;
    std::string language;
    std::string presentation;
    std::optional<std::string> play_manifest;
    std::optional<std::string> reference;
    EffectiveConditions conditions;
    std::vector<EffectiveValue> values; // what the launcher and the summary show (RF-1.6)
};

using EffectiveIssue = FieldIssue;

// Sources other than the options, already read by the caller.
struct ConditionSources {
    std::optional<Reference> play_manifest;
    std::optional<Reference> reference;
    std::optional<std::string> environment_core;
};

using EffectiveResult = std::variant<EffectiveRequest, std::vector<EffectiveIssue>>;

[[nodiscard]] EffectiveResult resolve_effective_request(const CheckOptions &options,
                                                        const ConditionSources &sources);
[[nodiscard]] const EffectiveValue *find_effective_value(const EffectiveRequest &request,
                                                         std::string_view key) noexcept;
[[nodiscard]] std::string_view value_source_code(ValueSource source) noexcept;

// Play CE (read only): the platform comes from the ROM extension with the table of
// AYTHER-Play-CE `library::platform_for_ext`, the core from `[cores]` and, if the
// platform has no entry, from `default_core`, as Play CE's `resolve_core` launches.
[[nodiscard]] std::optional<std::string_view>
play_ce_platform_for_extension(std::string_view extension) noexcept;
[[nodiscard]] std::optional<std::string> resolve_core_from_play_config(std::string_view config_text,
                                                                       std::string_view rom_path);
[[nodiscard]] std::optional<std::string> read_play_config_core(const std::filesystem::path &config,
                                                               std::string_view rom_path) noexcept;
[[nodiscard]] std::optional<std::filesystem::path> default_play_config_path();

} // namespace ayther::audio_qa
