#include "effective_values.h"

#include "model_limits.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <system_error>
#include <utility>

namespace ayther::audio_qa {

namespace {

constexpr std::uintmax_t max_play_config_bytes = 1024U * 1024U;

[[nodiscard]] const std::string *condition(const std::optional<Reference> &source,
                                           std::string_view key) noexcept {
    if (!source)
        return nullptr;
    for (const auto &item : source->conditions)
        if (item.key == key && item.value.value && !item.value.value->empty())
            return &*item.value.value;
    return nullptr;
}

[[nodiscard]] std::vector<std::string> conditions(const std::optional<Reference> &source,
                                                  std::string_view key) {
    std::vector<std::string> values;
    if (source)
        for (const auto &item : source->conditions)
            if (item.key == key && item.value.value && !item.value.value->empty())
                values.push_back(*item.value.value);
    return values;
}

[[nodiscard]] const std::string *material(const std::optional<Reference> &source,
                                          MaterialRole role) noexcept {
    if (!source)
        return nullptr;
    for (const auto &item : source->materials)
        if (item.role == role && item.source_locator.value && !item.source_locator.value->empty())
            return &*item.source_locator.value;
    return nullptr;
}

#ifdef _WIN32
[[nodiscard]] std::string lowercase(std::string_view text) {
    std::string result{text};
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}
#endif

// Same file when both exist; otherwise the same normalized path, ignoring case on
// Windows, where Play CE and the Lab write the paths.
[[nodiscard]] bool same_file(std::string_view left, std::string_view right) {
    const std::filesystem::path first{left};
    const std::filesystem::path second{right};
    std::error_code error;
    if (std::filesystem::exists(first, error) && std::filesystem::exists(second, error)) {
        const bool equivalent = std::filesystem::equivalent(first, second, error);
        if (!error)
            return equivalent;
    }
    const auto normalized_first = first.lexically_normal().generic_string();
    const auto normalized_second = second.lexically_normal().generic_string();
#ifdef _WIN32
    return lowercase(normalized_first) == lowercase(normalized_second);
#else
    return normalized_first == normalized_second;
#endif
}

[[nodiscard]] std::string option_key(std::string_view text) {
    const auto equals = text.find('=');
    return std::string{equals == std::string_view::npos ? text : text.substr(0, equals)};
}

class Builder final {
  public:
    explicit Builder(const ConditionSources &sources) : sources_(sources) {}

    void add(std::string key, std::string value, ValueSource source) {
        request.values.push_back({std::move(key), std::move(value), source});
    }

    // explicit > Play manifest > reference; nullopt when no source gives a value.
    std::optional<std::string> pick(const std::string &explicit_value, std::string_view key,
                                    std::string display) {
        if (!explicit_value.empty()) {
            add(std::move(display), explicit_value, ValueSource::explicit_option);
            return explicit_value;
        }
        if (const auto *value = condition(sources_.play_manifest, key)) {
            add(std::move(display), *value, ValueSource::play_manifest);
            return *value;
        }
        if (const auto *value = condition(sources_.reference, key)) {
            add(std::move(display), *value, ValueSource::reference);
            return *value;
        }
        return std::nullopt;
    }

    EffectiveRequest request;
    std::vector<EffectiveIssue> issues;

  private:
    const ConditionSources &sources_;
};

} // namespace

EffectiveResult resolve_effective_request(const CheckOptions &options,
                                          const ConditionSources &sources) {
    Builder builder{sources};
    auto &request = builder.request;

    request.runtime = options.runtime;
    builder.add("runtime", options.runtime, ValueSource::explicit_option);

    // RF-1.1, RF-1.2: the ROM is always the explicit one. Auxiliary sources that name
    // another ROM describe conditions of another game.
    request.rom = options.rom;
    builder.add("rom", options.rom, ValueSource::explicit_option);
    if (const auto *rom = material(sources.play_manifest, MaterialRole::rom);
        rom != nullptr && !same_file(*rom, options.rom))
        builder.issues.push_back({"--play-manifest", "play_manifest_rom_mismatch"});
    if (const auto *rom = material(sources.reference, MaterialRole::rom);
        rom != nullptr && !same_file(*rom, options.rom))
        builder.issues.push_back({"--reference", "reference_rom_mismatch"});

    // RF-1.6: the core may come from the environment, shown with its origin.
    if (!options.core.empty()) {
        request.core = options.core;
        builder.add("core", options.core, ValueSource::explicit_option);
    } else if (const auto *core = material(sources.play_manifest, MaterialRole::core)) {
        request.core = *core;
        builder.add("core", *core, ValueSource::play_manifest);
    } else if (const auto *reference_core = material(sources.reference, MaterialRole::core)) {
        request.core = *reference_core;
        builder.add("core", *reference_core, ValueSource::reference);
    } else if (sources.environment_core && !sources.environment_core->empty()) {
        request.core = *sources.environment_core;
        builder.add("core", *sources.environment_core, ValueSource::environment);
    } else {
        builder.issues.push_back({"--core", "core_unresolved"});
    }

    // RF-1.2, RF-1.4: explicit takes, in order and with their repetitions.
    request.takes = options.takes;
    for (std::size_t position = 0; position < options.takes.size(); ++position)
        builder.add("take[" + std::to_string(position) + "]", options.takes[position],
                    ValueSource::explicit_option);

    // RF-1.3: no pack means no pack, never one from another source. «Sin pack» is the
    // selection made by leaving out --pack, so it is explicit too: no ROM, take or pack
    // value has the default origin (RF-1.2).
    if (options.pack.empty()) {
        builder.add("pack", "none", ValueSource::explicit_option);
    } else {
        request.pack = options.pack;
        builder.add("pack", options.pack, ValueSource::explicit_option);
    }
    request.pack_mode = options.pack_mode;
    request.trust_registry = options.trust_registry;
    if (!options.trust_registry.empty())
        builder.add("trust_registry", options.trust_registry, ValueSource::explicit_option);
    request.output = options.output;
    builder.add("output", options.output, ValueSource::explicit_option);
    request.request_id = options.request_id;
    if (!options.request_id.empty())
        builder.add("request_id", options.request_id, ValueSource::explicit_option);
    // D-13 (RF-2.2, RNF-3): the request identity is what the ledger can keep, 256 bytes of
    // printable text. Anything else is found here, in its field, before the destination is
    // created; it never reaches the admission as an anonymous invalid request.
    if (options.request_id.size() > max_identity_bytes)
        builder.issues.push_back({"--request-id", "request_id_too_long"});
    else if (std::any_of(options.request_id.begin(), options.request_id.end(),
                         [](const unsigned char character) {
                             return character < 0x20U || character == 0x7fU;
                         }))
        builder.issues.push_back({"--request-id", "request_id_invalid"});
    request.language = options.language;
    request.presentation = options.presentation;
    if (!options.play_manifest.empty()) {
        request.play_manifest = options.play_manifest;
        builder.add("play_manifest", options.play_manifest, ValueSource::explicit_option);
    }
    if (!options.reference.empty()) {
        request.reference = options.reference;
        builder.add("reference", options.reference, ValueSource::explicit_option);
    }

    auto &conditions_out = request.conditions;
    conditions_out.profile = builder.pick(options.profile, "profile", "profile");
    // D-1 (RF-1.6, RF-3.5): the profile only applies to a loaded pack. Without one (or with
    // `--pack-mode original`) the request keeps the requested profile, so that the run without
    // pack has the same conditions as the run with it, and says that none is in effect. With a
    // pack, the preflight has checked that the pack offers it.
    if (conditions_out.profile) {
        const bool pack_loaded = !options.pack.empty() && options.pack_mode == "hd";
        const auto requested =
            std::find_if(builder.request.values.rbegin(), builder.request.values.rend(),
                         [](const EffectiveValue &value) { return value.key == "profile"; });
        builder.add("profile_effective", pack_loaded ? *conditions_out.profile : "none",
                    pack_loaded && requested != builder.request.values.rend()
                        ? requested->source
                        : ValueSource::generated);
    }
    conditions_out.subsystems = builder.pick(options.subsystems, "subsystems", "subsystems");
    conditions_out.mute_buses = builder.pick(options.mute_buses, "muted_buses", "mute_buses");
    conditions_out.video_output = builder.pick(options.video_output, "output", "video_output");
    conditions_out.patch = builder.pick(options.patch, "patch", "patch");
    const std::string explicit_shaders =
        options.shaders.empty() ? std::string{} : (options.shaders == "on" ? "true" : "false");
    if (const auto shaders = builder.pick(explicit_shaders, "shaders", "shaders")) {
        if (*shaders == "true")
            conditions_out.shaders = true;
        else if (*shaders == "false")
            conditions_out.shaders = false;
        else
            builder.issues.push_back({"--shaders", "invalid_shaders_condition"});
    }

    // Core options: the explicit list replaces every other source. Otherwise Play CE's
    // `no_flicker = true` becomes `no_sprite_limit=enabled`, as Play CE launches it,
    // followed by the reference options whose key is not already set.
    if (!options.core_options.empty()) {
        for (const auto &option : options.core_options) {
            conditions_out.core_options.push_back(option);
            builder.add("core_option", option, ValueSource::explicit_option);
        }
    } else {
        if (const auto *no_flicker = condition(sources.play_manifest, "no_flicker");
            no_flicker != nullptr && *no_flicker == "true") {
            conditions_out.core_options.emplace_back("no_sprite_limit=enabled");
            builder.add("core_option", "no_sprite_limit=enabled", ValueSource::play_manifest);
        }
        for (const auto &option : conditions(sources.reference, "core_option")) {
            const auto key = option_key(option);
            const bool present = std::any_of(
                conditions_out.core_options.begin(), conditions_out.core_options.end(),
                [&key](const std::string &existing) { return option_key(existing) == key; });
            if (present)
                continue;
            conditions_out.core_options.push_back(option);
            builder.add("core_option", option, ValueSource::reference);
        }
    }

    // RF-1.6 (D-4): the origin is whether the option was given, never a comparison with its
    // default. A value set without the parser (a client building the options) that differs from
    // the default is still explicit.
    const auto origin = [&options](std::string_view flag, const std::string &value,
                                   std::string_view default_value) {
        return options.was_given(flag) || value != default_value ? ValueSource::explicit_option
                                                                 : ValueSource::default_value;
    };
    builder.add("language", options.language, origin("--language", options.language, "es"));
    builder.add("presentation", options.presentation,
                origin("--presentation", options.presentation, "none"));
    builder.add("pack_mode", options.pack_mode, origin("--pack-mode", options.pack_mode, "hd"));

    if (!builder.issues.empty())
        return EffectiveResult{std::move(builder.issues)};
    return EffectiveResult{std::move(request)};
}

const EffectiveValue *find_effective_value(const EffectiveRequest &request,
                                           std::string_view key) noexcept {
    const auto found =
        std::find_if(request.values.begin(), request.values.end(),
                     [key](const EffectiveValue &value) { return value.key == key; });
    return found == request.values.end() ? nullptr : &*found;
}

std::string_view value_source_code(ValueSource source) noexcept {
    switch (source) {
    case ValueSource::explicit_option:
        return "explicit";
    case ValueSource::play_manifest:
        return "play_manifest";
    case ValueSource::reference:
        return "reference";
    case ValueSource::environment:
        return "environment";
    case ValueSource::default_value:
        return "default";
    case ValueSource::generated:
        return "generated";
    }
    return "unknown";
}

std::optional<std::string_view>
play_ce_platform_for_extension(std::string_view extension) noexcept {
    std::string lowered;
    lowered.reserve(extension.size());
    for (const char character : extension)
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    if (lowered == "md" || lowered == "gen" || lowered == "bin" || lowered == "rom")
        return "Mega Drive";
    if (lowered == "smc" || lowered == "sfc" || lowered == "fig")
        return "SNES";
    if (lowered == "nes" || lowered == "unf")
        return "NES";
    if (lowered == "gba")
        return "GBA";
    if (lowered == "n64" || lowered == "z64" || lowered == "v64")
        return "N64";
    if (lowered == "gb")
        return "Game Boy";
    if (lowered == "gbc")
        return "GBC";
    if (lowered == "pce")
        return "PC Engine";
    if (lowered == "sms" || lowered == "gg")
        return "Master System";
    return std::nullopt;
}

std::optional<std::string> resolve_core_from_play_config(std::string_view config_text,
                                                         std::string_view rom_path) {
    try {
        const auto table = toml::parse(config_text);
        auto extension = std::filesystem::path{rom_path}.extension().string();
        if (!extension.empty() && extension.front() == '.')
            extension.erase(extension.begin());
        if (const auto platform = play_ce_platform_for_extension(extension)) {
            if (const auto *cores = table["cores"].as_table()) {
                if (const auto core = (*cores)[*platform].value<std::string>();
                    core && !core->empty())
                    return *core;
            }
        }
        if (const auto core = table["default_core"].value<std::string>(); core && !core->empty())
            return *core;
        return std::nullopt;
    } catch (const toml::parse_error &) {
        return std::nullopt;
    }
}

std::optional<std::string> read_play_config_core(const std::filesystem::path &config,
                                                 std::string_view rom_path) noexcept {
    try {
        std::error_code error;
        if (!std::filesystem::is_regular_file(config, error) ||
            std::filesystem::file_size(config, error) > max_play_config_bytes || error)
            return std::nullopt;
        std::ifstream input{config, std::ios::binary};
        const std::string text{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
        return resolve_core_from_play_config(text, rom_path);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::filesystem::path> default_play_config_path() {
#ifdef _WIN32
    char *raw{};
    std::size_t size{};
    if (_dupenv_s(&raw, &size, "APPDATA") != 0 || raw == nullptr || size <= 1U) {
        std::free(raw);
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> value{raw, &std::free};
    return std::filesystem::path{value.get()} / "Ayther" / "play_config.toml";
#elif defined(__APPLE__)
    const auto *const home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0')
        return std::nullopt;
    return std::filesystem::path{home} / "Library" / "Application Support" / "Ayther" /
           "play_config.toml";
#else
    const auto *const home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0')
        return std::nullopt;
    return std::filesystem::path{home} / ".config" / "Ayther" / "play_config.toml";
#endif
}

} // namespace ayther::audio_qa
