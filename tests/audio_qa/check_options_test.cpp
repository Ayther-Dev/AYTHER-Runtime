// Spec 002: RF-1.1, RF-1.2, RF-1.3, RF-1.4 and RF-1.5 (contracts.md C5).
#include "check_options.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

std::vector<std::string_view> views(const std::vector<std::string> &values) {
    std::vector<std::string_view> result;
    result.reserve(values.size());
    for (const auto &value : values)
        result.emplace_back(value);
    return result;
}

std::vector<std::string> valid_arguments() {
    return {"--runtime",    "runtime.exe",      "--rom",
            "game.md",      "--core",           "core.dll",
            "--reference",  "reference.toml",   "--play-manifest",
            "play.toml",    "--pack",           "pack.ay",
            "--take",       "main.ayr",         "--take",
            "extra.arp",    "--output",         "evidence",
            "--request-id", "request-1",        "--language",
            "en",           "--trust-registry", "trust.toml"};
}

// The smallest admissible request (RF-1.1): runtime, ROM, one take and output.
std::vector<std::string> minimal_arguments() {
    return {"--runtime", "runtime.exe", "--rom",    "game.md",
            "--take",    "main.ayr",    "--output", "evidence"};
}

std::vector<std::string> without(std::vector<std::string> arguments, std::string_view option) {
    for (auto iterator = arguments.begin(); iterator != arguments.end();) {
        if (*iterator == option)
            iterator = arguments.erase(iterator, iterator + 2);
        else
            ++iterator;
    }
    return arguments;
}

bool has_error(const std::vector<std::string> &arguments, qa::CheckOptionErrorCode code,
               std::string_view option) {
    const auto argument_views = views(arguments);
    const auto result = qa::parse_check_options(argument_views);
    const auto *error = result.error();
    return error != nullptr && error->code == code && error->option == option;
}

const qa::CheckOptions *parsed(const std::vector<std::string> &arguments,
                               qa::CheckOptionsParseResult &storage) {
    storage = qa::parse_check_options(views(arguments));
    return storage.options();
}

bool test_valid() {
    auto storage = qa::parse_check_options({});
    const auto *options = parsed(valid_arguments(), storage);
    return options != nullptr && options->runtime == "runtime.exe" && options->rom == "game.md" &&
           options->core == "core.dll" && options->reference == "reference.toml" &&
           options->play_manifest == "play.toml" && options->pack == "pack.ay" &&
           options->takes == std::vector<std::string>{"main.ayr", "extra.arp"} &&
           options->output == "evidence" && options->request_id == "request-1" &&
           options->language == "en" && options->trust_registry == "trust.toml" &&
           options->pack_mode == "hd";
}

// RF-1.1, RF-1.3: the pack, the reference and the Play manifest are optional.
bool test_minimal_request() {
    auto storage = qa::parse_check_options({});
    const auto *options = parsed(minimal_arguments(), storage);
    return options != nullptr && options->pack.empty() && options->reference.empty() &&
           options->play_manifest.empty() && options->core.empty() &&
           options->takes == std::vector<std::string>{"main.ayr"};
}

// RF-1.1, RF-1.2: runtime, ROM, output and at least one take are required; an empty
// value is never accepted.
bool test_required_options() {
    constexpr std::array required{"--runtime", "--rom", "--output", "--take"};
    for (const std::string_view missing : required) {
        if (!has_error(without(minimal_arguments(), missing),
                       qa::CheckOptionErrorCode::missing_required_option, missing))
            return false;
        auto arguments = minimal_arguments();
        const auto iterator = std::find(arguments.begin(), arguments.end(), missing);
        if (iterator == arguments.end())
            return false;
        *(iterator + 1) = "";
        if (!has_error(arguments, qa::CheckOptionErrorCode::empty_value, missing))
            return false;
    }
    for (const std::string_view optional : {"--core", "--pack", "--reference", "--play-manifest"}) {
        auto arguments = valid_arguments();
        const auto iterator = std::find(arguments.begin(), arguments.end(), optional);
        if (iterator == arguments.end())
            return false;
        *(iterator + 1) = "";
        if (!has_error(arguments, qa::CheckOptionErrorCode::empty_value, optional))
            return false;
    }
    return true;
}

// RF-1.4: repeated takes are kept in order.
bool test_repeated_takes() {
    auto arguments = minimal_arguments();
    arguments.insert(arguments.end(), {"--take", "other.ayr", "--take", "main.ayr"});
    auto storage = qa::parse_check_options({});
    const auto *options = parsed(arguments, storage);
    return options != nullptr &&
           options->takes == std::vector<std::string>{"main.ayr", "other.ayr", "main.ayr"};
}

// RF-1.5: the conditions that used to arrive only through the manifest are options.
bool test_conditions() {
    auto arguments = minimal_arguments();
    arguments.insert(arguments.end(),
                     {"--profile", "full", "--subsystems", "7", "--mute-buses", "4294967295",
                      "--video-output", "lcd", "--patch", "patch.ips", "--shaders", "off",
                      "--core-option", "no_sprite_limit=enabled", "--core-option", "region=us"});
    auto storage = qa::parse_check_options({});
    const auto *options = parsed(arguments, storage);
    if (options == nullptr || options->profile != "full" || options->subsystems != "7" ||
        options->mute_buses != "4294967295" || options->video_output != "lcd" ||
        options->patch != "patch.ips" || options->shaders != "off" ||
        options->core_options != std::vector<std::string>{"no_sprite_limit=enabled", "region=us"})
        return false;

    auto too_large = minimal_arguments();
    too_large.insert(too_large.end(), {"--subsystems", "4294967296"});
    auto not_number = minimal_arguments();
    not_number.insert(not_number.end(), {"--mute-buses", "4x"});
    auto shaders = minimal_arguments();
    shaders.insert(shaders.end(), {"--shaders", "maybe"});
    auto core_option = minimal_arguments();
    core_option.insert(core_option.end(), {"--core-option", "=value"});
    auto no_equals = minimal_arguments();
    no_equals.insert(no_equals.end(), {"--core-option", "flag"});
    return has_error(too_large, qa::CheckOptionErrorCode::invalid_unsigned_value, "--subsystems") &&
           has_error(not_number, qa::CheckOptionErrorCode::invalid_unsigned_value,
                     "--mute-buses") &&
           has_error(shaders, qa::CheckOptionErrorCode::unsupported_shaders, "--shaders") &&
           has_error(core_option, qa::CheckOptionErrorCode::malformed_core_option,
                     "--core-option") &&
           has_error(no_equals, qa::CheckOptionErrorCode::malformed_core_option, "--core-option");
}

bool test_malformed() {
    auto missing_value = valid_arguments();
    const auto pack = std::find(missing_value.begin(), missing_value.end(), "--pack");
    missing_value.erase(pack + 1);
    if (!has_error(missing_value, qa::CheckOptionErrorCode::missing_value, "--pack"))
        return false;

    auto duplicate = valid_arguments();
    duplicate.insert(duplicate.end(), {"--runtime", "other.exe"});
    if (!has_error(duplicate, qa::CheckOptionErrorCode::duplicate_option, "--runtime"))
        return false;
    auto duplicate_rom = valid_arguments();
    duplicate_rom.insert(duplicate_rom.end(), {"--rom", "other.md"});
    if (!has_error(duplicate_rom, qa::CheckOptionErrorCode::duplicate_option, "--rom"))
        return false;

    auto unknown = valid_arguments();
    unknown.insert(unknown.end(), {"--mystery", "value"});
    if (!has_error(unknown, qa::CheckOptionErrorCode::unknown_option, "--mystery"))
        return false;

    auto unsupported_language = valid_arguments();
    const auto language =
        std::find(unsupported_language.begin(), unsupported_language.end(), "--language");
    if (language == unsupported_language.end())
        return false;
    *(language + 1) = "fr";
    return has_error(unsupported_language, qa::CheckOptionErrorCode::unsupported_language,
                     "--language");
}

bool test_original_audio_mode() {
    auto arguments = valid_arguments();
    arguments.insert(arguments.end(), {"--pack-mode", "original"});
    const auto parsed_original = qa::parse_check_options(views(arguments));
    if (!parsed_original.options() || parsed_original.options()->pack_mode != "original")
        return false;
    arguments.back() = "invalid";
    const auto rejected = qa::parse_check_options(views(arguments));
    return rejected.error() != nullptr &&
           rejected.error()->code == qa::CheckOptionErrorCode::unsupported_pack_mode;
}

bool test_presentation() {
    auto arguments = valid_arguments();
    arguments.insert(arguments.end(), {"--presentation", "visible"});
    const auto visible = qa::parse_check_options(views(arguments));
    if (!visible.options() || visible.options()->presentation != "visible")
        return false;
    auto duplicate = arguments;
    duplicate.insert(duplicate.end(), {"--presentation", "none"});
    if (!has_error(duplicate, qa::CheckOptionErrorCode::duplicate_option, "--presentation"))
        return false;
    arguments.back() = "invalid";
    return qa::parse_check_options(views(arguments)).error() != nullptr;
}

} // namespace

int main() {
    return test_valid() && test_minimal_request() && test_required_options() &&
                   test_repeated_takes() && test_conditions() && test_malformed() &&
                   test_original_audio_mode() && test_presentation()
               ? 0
               : 1;
}
