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
    return {"--runtime", "runtime.exe", "--reference", "reference.toml",   "--play-manifest",
            "play.toml", "--pack",      "pack.ay",     "--take",           "main.ayr",
            "--take",    "extra.arp",   "--output",    "evidence",         "--request-id",
            "request-1", "--language",  "en",          "--trust-registry", "trust.toml"};
}

bool has_error(const std::vector<std::string> &arguments, qa::CheckOptionErrorCode code,
               std::string_view option) {
    const auto argument_views = views(arguments);
    const auto result = qa::parse_check_options(argument_views);
    const auto *error = result.error();
    return error != nullptr && error->code == code && error->option == option;
}

bool test_valid() {
    const auto arguments = valid_arguments();
    const auto argument_views = views(arguments);
    const auto result = qa::parse_check_options(argument_views);
    const auto *options = result.options();
    return options != nullptr && options->runtime == "runtime.exe" &&
           options->reference == "reference.toml" && options->play_manifest == "play.toml" &&
           options->pack == "pack.ay" &&
           options->takes == std::vector<std::string>{"main.ayr", "extra.arp"} &&
           options->output == "evidence" && options->request_id == "request-1" &&
           options->language == "en" && options->trust_registry == "trust.toml" &&
           options->pack_mode == "hd";
}

bool test_original_audio_mode() {
    auto arguments = valid_arguments();
    arguments.insert(arguments.end(), {"--pack-mode", "original"});
    const auto parsed = qa::parse_check_options(views(arguments));
    if (!parsed.options() || parsed.options()->pack_mode != "original")
        return false;
    arguments.back() = "invalid";
    const auto rejected = qa::parse_check_options(views(arguments));
    return rejected.error() != nullptr &&
           rejected.error()->code == qa::CheckOptionErrorCode::unsupported_pack_mode;
}

bool test_required_options() {
    constexpr std::array required{"--runtime", "--reference", "--play-manifest", "--pack",
                                  "--output"};
    for (const std::string_view missing : required) {
        auto arguments = valid_arguments();
        for (auto iterator = arguments.begin(); iterator != arguments.end();) {
            if (*iterator == missing) {
                iterator = arguments.erase(iterator, iterator + 2);
                break;
            } else {
                ++iterator;
            }
        }
        if (!has_error(arguments, qa::CheckOptionErrorCode::missing_required_option, missing))
            return false;

        arguments = valid_arguments();
        const auto iterator = std::find(arguments.begin(), arguments.end(), missing);
        if (iterator == arguments.end())
            return false;
        *(iterator + 1) = "";
        if (!has_error(arguments, qa::CheckOptionErrorCode::empty_value, missing))
            return false;
    }
    auto empty_take = valid_arguments();
    const auto take = std::find(empty_take.begin(), empty_take.end(), "--take");
    if (take == empty_take.end())
        return false;
    *(take + 1) = "";
    return has_error(empty_take, qa::CheckOptionErrorCode::empty_value, "--take");
}

bool test_malformed() {
    auto missing_value = valid_arguments();
    missing_value.erase(missing_value.begin() + 7);
    if (!has_error(missing_value, qa::CheckOptionErrorCode::missing_value, "--pack"))
        return false;

    auto duplicate = valid_arguments();
    duplicate.insert(duplicate.end(), {"--runtime", "other.exe"});
    if (!has_error(duplicate, qa::CheckOptionErrorCode::duplicate_option, "--runtime"))
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

} // namespace

int main() {
    auto arguments = valid_arguments();
    arguments.insert(arguments.end(), {"--presentation", "visible"});
    const auto visible = qa::parse_check_options(views(arguments));
    if (!visible.options() || visible.options()->presentation != "visible")
        return 1;
    auto duplicate = arguments;
    duplicate.insert(duplicate.end(), {"--presentation", "none"});
    if (!has_error(duplicate, qa::CheckOptionErrorCode::duplicate_option, "--presentation"))
        return 1;
    arguments.back() = "invalid";
    if (!qa::parse_check_options(views(arguments)).error())
        return 1;
    return test_valid() && test_required_options() && test_malformed() &&
                   test_original_audio_mode()
               ? 0
               : 1;
}
