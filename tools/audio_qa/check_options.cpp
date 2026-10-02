#include "check_options.h"

#include <array>
#include <optional>
#include <utility>

namespace ayther::audio_qa {

namespace {

[[nodiscard]] CheckOptionError make_error(CheckOptionErrorCode code, std::string_view option,
                                          std::size_t index) {
    return {code, std::string{option}, index};
}

[[nodiscard]] bool is_option(std::string_view argument) noexcept {
    return argument.starts_with("--");
}

} // namespace

CheckOptionsParseResult::CheckOptionsParseResult(CheckOptions options)
    : result_(std::move(options)) {}

CheckOptionsParseResult::CheckOptionsParseResult(CheckOptionError error)
    : result_(std::move(error)) {}

const CheckOptions *CheckOptionsParseResult::options() const noexcept {
    return std::get_if<CheckOptions>(&result_);
}

const CheckOptionError *CheckOptionsParseResult::error() const noexcept {
    return std::get_if<CheckOptionError>(&result_);
}

CheckOptionsParseResult parse_check_options(std::span<const std::string_view> arguments) {
    if (arguments.size() > max_check_arguments)
        return CheckOptionsParseResult{
            make_error(CheckOptionErrorCode::too_many_arguments, {}, arguments.size())};

    CheckOptions options;
    std::array<bool, 10> seen{};

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto option = arguments[index];
        if (!is_option(option))
            return CheckOptionsParseResult{
                make_error(CheckOptionErrorCode::unknown_option, option, index)};
        if (index + 1 >= arguments.size() || is_option(arguments[index + 1]))
            return CheckOptionsParseResult{
                make_error(CheckOptionErrorCode::missing_value, option, index)};

        const auto value = arguments[++index];
        if (value.empty())
            return CheckOptionsParseResult{
                make_error(CheckOptionErrorCode::empty_value, option, index)};
        if (value.size() > max_check_option_value_bytes)
            return CheckOptionsParseResult{
                make_error(CheckOptionErrorCode::value_too_long, option, index)};

        std::string *destination = nullptr;
        std::size_t slot = 0;
        if (option == "--runtime") {
            destination = &options.runtime;
            slot = 0;
        } else if (option == "--reference") {
            destination = &options.reference;
            slot = 1;
        } else if (option == "--play-manifest") {
            destination = &options.play_manifest;
            slot = 2;
        } else if (option == "--pack") {
            destination = &options.pack;
            slot = 3;
        } else if (option == "--output") {
            destination = &options.output;
            slot = 4;
        } else if (option == "--request-id") {
            destination = &options.request_id;
            slot = 5;
        } else if (option == "--language") {
            destination = &options.language;
            slot = 6;
        } else if (option == "--trust-registry") {
            destination = &options.trust_registry;
            slot = 7;
        } else if (option == "--presentation") {
            destination = &options.presentation;
            slot = 8;
        } else if (option == "--pack-mode") {
            destination = &options.pack_mode;
            slot = 9;
        } else if (option == "--take") {
            if (options.takes.size() == max_check_takes)
                return CheckOptionsParseResult{
                    make_error(CheckOptionErrorCode::too_many_takes, option, index)};
            options.takes.emplace_back(value);
            continue;
        } else {
            return CheckOptionsParseResult{
                make_error(CheckOptionErrorCode::unknown_option, option, index - 1)};
        }

        if (seen[slot])
            return CheckOptionsParseResult{
                make_error(CheckOptionErrorCode::duplicate_option, option, index - 1)};
        seen[slot] = true;
        destination->assign(value);
    }

    const std::array required{
        std::pair{std::string_view{"--runtime"}, options.runtime.empty()},
        std::pair{std::string_view{"--reference"}, options.reference.empty()},
        std::pair{std::string_view{"--play-manifest"}, options.play_manifest.empty()},
        std::pair{std::string_view{"--pack"}, options.pack.empty()},
        std::pair{std::string_view{"--output"}, options.output.empty()},
    };
    for (const auto &[option, missing] : required)
        if (missing)
            return CheckOptionsParseResult{make_error(CheckOptionErrorCode::missing_required_option,
                                                      option, arguments.size())};

    if (options.language != "es" && options.language != "en")
        return CheckOptionsParseResult{
            make_error(CheckOptionErrorCode::unsupported_language, "--language", arguments.size())};

    if (options.presentation != "none" && options.presentation != "visible")
        return CheckOptionsParseResult{make_error(CheckOptionErrorCode::unsupported_presentation,
                                                  "--presentation", arguments.size())};
    if (options.pack_mode != "hd" && options.pack_mode != "original")
        return CheckOptionsParseResult{make_error(CheckOptionErrorCode::unsupported_pack_mode,
                                                  "--pack-mode", arguments.size())};
    return CheckOptionsParseResult{std::move(options)};
}

std::string_view check_option_error_code(CheckOptionErrorCode code) noexcept {
    switch (code) {
    case CheckOptionErrorCode::too_many_arguments:
        return "too_many_arguments";
    case CheckOptionErrorCode::unknown_option:
        return "unknown_option";
    case CheckOptionErrorCode::missing_value:
        return "missing_option_value";
    case CheckOptionErrorCode::empty_value:
        return "empty_option_value";
    case CheckOptionErrorCode::value_too_long:
        return "option_value_too_long";
    case CheckOptionErrorCode::duplicate_option:
        return "duplicate_option";
    case CheckOptionErrorCode::too_many_takes:
        return "too_many_takes";
    case CheckOptionErrorCode::missing_required_option:
        return "missing_required_option";
    case CheckOptionErrorCode::unsupported_language:
        return "unsupported_language";
    case CheckOptionErrorCode::invalid_number:
        return "invalid_number";
    case CheckOptionErrorCode::unsupported_presentation:
        return "unsupported_presentation";
    case CheckOptionErrorCode::unsupported_pack_mode:
        return "unsupported_pack_mode";
    }
    return "invalid_option";
}

} // namespace ayther::audio_qa
