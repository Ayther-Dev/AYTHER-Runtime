// Spec 002: RF-1.1, RF-1.2, RF-1.3, RF-1.5 and RF-1.6. Effective values, their
// origin and the Runtime command line built from them.
#include "check_execution.h"
#include "content_hash.h"
#include "effective_values.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

qa::CheckOptions minimal_options() {
    qa::CheckOptions options;
    options.runtime = "runtime.exe";
    options.rom = "C:/roms/game.md";
    options.core = "C:/cores/core.dll";
    options.takes = {"C:/takes/a.ayr"};
    options.output = "evidence";
    return options;
}

qa::Material material(qa::MaterialRole role, std::string path) {
    qa::Material item;
    item.material_id = "m";
    item.role = role;
    item.source_locator.value = std::move(path);
    return item;
}

qa::Reference source(std::string rom, std::string core,
                     std::vector<std::pair<std::string, std::string>> conditions) {
    qa::Reference reference;
    reference.materials.push_back(material(qa::MaterialRole::rom, std::move(rom)));
    reference.materials.push_back(material(qa::MaterialRole::core, std::move(core)));
    for (auto &[key, value] : conditions) {
        qa::EffectiveCondition condition;
        condition.key = key;
        condition.value.value = value;
        reference.conditions.push_back(std::move(condition));
    }
    return reference;
}

const qa::EffectiveRequest *resolved(const qa::EffectiveResult &result) {
    return std::get_if<qa::EffectiveRequest>(&result);
}

bool has_value(const qa::EffectiveRequest &request, std::string_view key, std::string_view value,
               qa::ValueSource source) {
    return std::any_of(request.values.begin(), request.values.end(), [&](const auto &item) {
        return item.key == key && item.value == value && item.source == source;
    });
}

void minimal_request_is_admissible() {
    const auto result = qa::resolve_effective_request(minimal_options(), {});
    const auto *request = resolved(result);
    expect(request != nullptr, "RF-1.1: ROM, take, core, runtime and output are enough");
    if (request == nullptr)
        return;
    expect(!request->pack && has_value(*request, "pack", "none", qa::ValueSource::explicit_option),
           "RF-1.3: no pack is shown as none");
    expect(has_value(*request, "rom", "C:/roms/game.md", qa::ValueSource::explicit_option) &&
               has_value(*request, "take[0]", "C:/takes/a.ayr", qa::ValueSource::explicit_option),
           "RF-1.6: ROM and take are explicit");
    expect(!request->conditions.profile && !request->conditions.shaders &&
               request->conditions.core_options.empty(),
           "RF-1.6: no condition is invented without a source");
}

void precedence_by_origin() {
    const auto manifest = source(
        "C:/roms/game.md", "C:/cores/manifest.dll",
        {{"profile", "full"}, {"output", "lcd"}, {"shaders", "true"}, {"no_flicker", "true"}});
    const auto reference = source("C:/roms/game.md", "C:/cores/reference.dll",
                                  {{"profile", "lite"},
                                   {"muted_buses", "4"},
                                   {"core_option", "no_sprite_limit=disabled"},
                                   {"core_option", "region=auto"}});

    auto options = minimal_options();
    options.core.clear();
    qa::ConditionSources sources{manifest, reference, std::string{"C:/cores/environment.dll"}};
    auto result = qa::resolve_effective_request(options, sources);
    const auto *request = resolved(result);
    expect(request != nullptr, "RF-1.6: auxiliary sources resolve");
    if (request == nullptr)
        return;
    expect(request->core == "C:/cores/manifest.dll" &&
               has_value(*request, "core", "C:/cores/manifest.dll", qa::ValueSource::play_manifest),
           "RF-1.6: the manifest core precedes reference and environment");
    expect(request->conditions.profile == "full" &&
               has_value(*request, "profile", "full", qa::ValueSource::play_manifest),
           "RF-1.6: the manifest condition precedes the reference");
    expect(request->conditions.mute_buses == "4" &&
               has_value(*request, "mute_buses", "4", qa::ValueSource::reference),
           "RF-1.6: the reference fills what the manifest lacks");
    expect(request->conditions.shaders == true && request->conditions.video_output == "lcd",
           "RF-1.6: shaders and output come from the manifest");
    expect(request->conditions.core_options ==
               std::vector<std::string>{"no_sprite_limit=enabled", "region=auto"},
           "RF-1.6: Play no_flicker becomes no_sprite_limit and wins over the reference key");

    options.profile = "custom";
    options.shaders = "off";
    options.core_options = {"region=us"};
    result = qa::resolve_effective_request(options, sources);
    request = resolved(result);
    expect(request != nullptr && request->conditions.profile == "custom" &&
               has_value(*request, "profile", "custom", qa::ValueSource::explicit_option) &&
               request->conditions.shaders == false &&
               request->conditions.core_options == std::vector<std::string>{"region=us"},
           "RF-1.6: an explicit option wins over the manifest");

    options = minimal_options();
    options.core.clear();
    result = qa::resolve_effective_request(
        options, {std::nullopt, std::nullopt, std::string{"C:/cores/environment.dll"}});
    request = resolved(result);
    expect(request != nullptr && has_value(*request, "core", "C:/cores/environment.dll",
                                           qa::ValueSource::environment),
           "RF-1.6: the environment resolves the core last");

    result = qa::resolve_effective_request(options, {});
    const auto *issues = std::get_if<std::vector<qa::EffectiveIssue>>(&result);
    expect(issues != nullptr && issues->front() == qa::EffectiveIssue{"--core", "core_unresolved"},
           "RF-1.7: an unresolved core blocks the request");
}

// RF-1.6: what nobody selected keeps its documented default and says so.
void default_origin() {
    const auto result = qa::resolve_effective_request(minimal_options(), {});
    const auto *request = resolved(result);
    expect(request != nullptr &&
               has_value(*request, "language", "es", qa::ValueSource::default_value) &&
               has_value(*request, "presentation", "none", qa::ValueSource::default_value) &&
               has_value(*request, "pack_mode", "hd", qa::ValueSource::default_value),
           "RF-1.6: unselected values show the default origin");
}

// D-4 (campaign 2026-10-04, BR-182; RF-1.6): the origin says whether the option was given,
// not whether its value equals the default. `--language es`, `--presentation none` and
// `--pack-mode hd` written on the command line are explicit.
void explicit_value_equal_to_default() {
    const std::vector<std::string> given{"--runtime",      "runtime.exe",
                                         "--rom",          "C:/roms/game.md",
                                         "--core",         "C:/cores/core.dll",
                                         "--take",         "C:/takes/a.ayr",
                                         "--output",       "evidence",
                                         "--language",     "es",
                                         "--presentation", "none",
                                         "--pack-mode",    "hd"};
    std::vector<std::string_view> arguments{given.begin(), given.end()};
    const auto parsed = qa::parse_check_options(arguments);
    expect(parsed.options() != nullptr, "the explicit defaults parse");
    if (parsed.options() == nullptr)
        return;
    const auto result = qa::resolve_effective_request(*parsed.options(), {});
    const auto *request = resolved(result);
    expect(
        request != nullptr &&
            has_value(*request, "language", "es", qa::ValueSource::explicit_option) &&
            has_value(*request, "presentation", "none", qa::ValueSource::explicit_option) &&
            has_value(*request, "pack_mode", "hd", qa::ValueSource::explicit_option),
        "RF-1.6: a value given explicitly keeps the explicit origin even when it is the default");

    // The same command line without them keeps the default origin.
    arguments.resize(arguments.size() - 6U);
    const auto omitted = qa::parse_check_options(arguments);
    const auto omitted_result = omitted.options() != nullptr
                                    ? qa::resolve_effective_request(*omitted.options(), {})
                                    : qa::EffectiveResult{std::vector<qa::EffectiveIssue>{}};
    const auto *defaults = resolved(omitted_result);
    expect(defaults != nullptr &&
               has_value(*defaults, "language", "es", qa::ValueSource::default_value) &&
               has_value(*defaults, "presentation", "none", qa::ValueSource::default_value) &&
               has_value(*defaults, "pack_mode", "hd", qa::ValueSource::default_value),
           "RF-1.6: an option that was not given keeps the default origin");
}

// RF-1.2, RF-1.3: ROM, takes and pack only have the explicit origin, even when every
// auxiliary source names one.
void selections_are_only_explicit() {
    auto manifest = source("C:/roms/game.md", "C:/cores/manifest.dll", {});
    manifest.materials.push_back(material(qa::MaterialRole::pack, "C:/packs/manifest.ay"));
    manifest.materials.push_back(material(qa::MaterialRole::take, "C:/takes/manifest.ayr"));
    auto reference = source("C:/roms/game.md", "C:/cores/reference.dll", {});
    reference.materials.push_back(material(qa::MaterialRole::pack, "C:/packs/reference.ay"));
    const auto result = qa::resolve_effective_request(
        minimal_options(), {manifest, reference, std::string{"C:/cores/environment.dll"}});
    const auto *request = resolved(result);
    expect(request != nullptr, "RF-1.2: auxiliary sources with selections still resolve");
    if (request == nullptr)
        return;
    expect(!request->pack && request->takes == std::vector<std::string>{"C:/takes/a.ayr"},
           "RF-1.2, RF-1.3: no pack or take is taken from an auxiliary source");
    const bool only_explicit =
        std::all_of(request->values.begin(), request->values.end(), [](const auto &value) {
            const bool selection =
                value.key == "rom" || value.key == "pack" || value.key.rfind("take[", 0) == 0;
            return !selection || value.source == qa::ValueSource::explicit_option;
        });
    expect(only_explicit, "RF-1.2: ROM, take and pack values are always explicit");
}

void rom_is_only_explicit() {
    auto options = minimal_options();
    const auto other = source("C:/roms/other.md", "C:/cores/core.dll", {});
    const auto result = qa::resolve_effective_request(options, {other, std::nullopt, {}});
    const auto *issues = std::get_if<std::vector<qa::EffectiveIssue>>(&result);
    expect(issues != nullptr && issues->front() == qa::EffectiveIssue{"--play-manifest",
                                                                      "play_manifest_rom_mismatch"},
           "RF-1.2, RF-1.7: a manifest for another ROM is an incompatibility, not a source");
}

std::string read_bytes(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

qa::ContentIdentity hash(const std::string &bytes) {
    return qa::identify_content(std::as_bytes(std::span{bytes.data(), bytes.size()}));
}

void play_config_resolution() {
    const auto config = std::string{"default_core = \"C:/cores/default.dll\"\n"
                                    "[cores]\n\"Mega Drive\" = \"C:/cores/gpgx.dll\"\n"};
    expect(qa::resolve_core_from_play_config(config, "C:/roms/Sonic.md") == "C:/cores/gpgx.dll",
           "RF-1.6: the platform core is used");
    expect(qa::resolve_core_from_play_config(config, "C:/roms/Mario.sfc") == "C:/cores/default.dll",
           "RF-1.6: another platform falls back to default_core");
    expect(!qa::resolve_core_from_play_config("[cores]\n", "C:/roms/Sonic.md"),
           "RF-1.6: no configured core resolves nothing");

    const auto path = std::filesystem::temp_directory_path() / "ayther-qa-play_config-test.toml";
    {
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        output << config;
    }
    const auto before = hash(read_bytes(path));
    expect(qa::read_play_config_core(path, "C:/roms/Sonic.md") == "C:/cores/gpgx.dll",
           "RF-1.6: the configuration file is read");
    expect(hash(read_bytes(path)) == before, "RF-1.6: the Play CE configuration keeps its SHA-256");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    expect(!qa::read_play_config_core(path, "C:/roms/Sonic.md"),
           "RF-1.6: a missing configuration resolves nothing");
}

bool contains(const std::vector<std::wstring> &arguments, std::wstring_view value) {
    return std::find(arguments.begin(), arguments.end(), value) != arguments.end();
}

void runtime_command_line() {
    auto options = minimal_options();
    auto result = qa::resolve_effective_request(options, {});
    auto request = *resolved(result);
    auto arguments = qa::runtime_replay_arguments(request, "11", "22", "run-1");
    expect(!contains(arguments, L"--pack") && !contains(arguments, L"--manifest"),
           "RF-1.3: no pack and no manifest reach the Runtime when none was selected");
    expect(contains(arguments, L"--rom") && contains(arguments, L"C:/roms/game.md") &&
               contains(arguments, L"--core") && contains(arguments, L"C:/cores/core.dll"),
           "RF-1.1: ROM and core reach the Runtime");

    options.pack = "C:/packs/game.ay";
    options.profile = "full";
    options.subsystems = "7";
    options.mute_buses = "2";
    options.video_output = "crt";
    options.patch = "C:/patches/p.ips";
    options.shaders = "on";
    options.core_options = {"no_sprite_limit=enabled", "region=us"};
    result = qa::resolve_effective_request(options, {});
    request = *resolved(result);
    arguments = qa::runtime_replay_arguments(request, "11", "22", "run-1");
    expect(contains(arguments, L"--pack") && contains(arguments, L"C:/packs/game.ay"),
           "RF-1.3: a selected pack reaches the Runtime");
    expect(contains(arguments, L"--profile") && contains(arguments, L"full") &&
               contains(arguments, L"--subsystems") && contains(arguments, L"7") &&
               contains(arguments, L"--mute-buses") && contains(arguments, L"2") &&
               contains(arguments, L"--output") && contains(arguments, L"crt") &&
               contains(arguments, L"--patch") && contains(arguments, L"C:/patches/p.ips") &&
               contains(arguments, L"--shaders") &&
               contains(arguments, L"no_sprite_limit=enabled") && contains(arguments, L"region=us"),
           "RF-1.5: every explicit condition reaches the Runtime");

    request.pack_mode = "original";
    arguments = qa::runtime_replay_arguments(request, "11", "22", "run-1");
    expect(!contains(arguments, L"--pack"), "RNF-6: --pack-mode original keeps the pack unloaded");

    options.shaders = "off";
    result = qa::resolve_effective_request(options, {});
    arguments = qa::runtime_replay_arguments(*resolved(result), "11", "22", "run-1");
    expect(contains(arguments, L"--no-shaders") && !contains(arguments, L"--shaders"),
           "RF-1.5: --shaders off reaches the Runtime as --no-shaders");

    // BR-135 (contracts.md C1-3, plan §5.11): the Runtime learns the position of its take
    // and whether it is the last one.
    arguments = qa::runtime_replay_arguments(*resolved(result), "11", "22", "run-1", 3U, true);
    const auto position = std::find(arguments.begin(), arguments.end(), L"--qa-take-position");
    expect(position != arguments.end() && std::next(position) != arguments.end() &&
               *std::next(position) == L"3" && contains(arguments, L"--qa-last-take"),
           "RF-2.3: the take position and the last take reach the Runtime");
    arguments = qa::runtime_replay_arguments(*resolved(result), "11", "22", "run-1", 0U, false);
    expect(!contains(arguments, L"--qa-last-take"), "an intermediate take is not the last one");
}

// D-13 (campaign 2026-10-05, BR-191; RF-2.2, RNF-3): the request identity holds 256 bytes of
// printable text. Beyond that, or with a control character, the request is rejected in the
// validation, in the field --request-id, before anything is created.
void request_identity_limits() {
    auto options = minimal_options();
    options.request_id = std::string(256U, 'r');
    const auto at_limit = qa::resolve_effective_request(options, {});
    expect(std::holds_alternative<qa::EffectiveRequest>(at_limit),
           "D-13, RNF-3: a request identity of 256 bytes is accepted");
    options.request_id = std::string(257U, 'r');
    const auto beyond = qa::resolve_effective_request(options, {});
    const auto *too_long = std::get_if<std::vector<qa::EffectiveIssue>>(&beyond);
    expect(too_long != nullptr && too_long->size() == 1U &&
               too_long->front() == qa::EffectiveIssue{"--request-id", "request_id_too_long"},
           "D-13, RF-2.2: a request identity of 257 bytes is rejected in --request-id");
    options.request_id = std::string{"request"} + char{0x09} + "id";
    const auto control = qa::resolve_effective_request(options, {});
    const auto *invalid = std::get_if<std::vector<qa::EffectiveIssue>>(&control);
    expect(invalid != nullptr && invalid->size() == 1U &&
               invalid->front() == qa::EffectiveIssue{"--request-id", "request_id_invalid"},
           "D-13, RF-2.2: a request identity with a control character is rejected in "
           "--request-id");
}

} // namespace

int main() {
    minimal_request_is_admissible();
    precedence_by_origin();
    default_origin();
    explicit_value_equal_to_default();
    selections_are_only_explicit();
    rom_is_only_explicit();
    play_config_resolution();
    runtime_command_line();
    request_identity_limits();
    if (failures != 0)
        return 1;
    std::cout << "effective values keep their origin\n";
    return 0;
}
