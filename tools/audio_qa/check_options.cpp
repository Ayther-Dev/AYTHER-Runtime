#include "check_options.h"

#include "check_option_descriptors.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace ayther::audio_qa {

namespace {

[[nodiscard]] CheckOptionError make_error(CheckOptionErrorCode code, std::string_view option,
                                          std::size_t index) {
    return {code, std::string{option}, index};
}

[[nodiscard]] bool is_option(std::string_view argument) noexcept {
    return argument.starts_with("--");
}

// Decimal digits only, within uint32 (the Runtime parses --subsystems and --mute-buses so).
[[nodiscard]] bool valid_unsigned_32(std::string_view value) noexcept {
    if (value.empty() || value.size() > 10U)
        return false;
    std::uint64_t parsed = 0;
    for (const char digit : value) {
        if (digit < '0' || digit > '9')
            return false;
        parsed = parsed * 10U + static_cast<std::uint64_t>(digit - '0');
    }
    return parsed <= 0xffffffffULL;
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

    // Spec 002 (RF-1.5, contracts.md C5): every rule comes from the descriptor table.
    const auto descriptors = check_option_descriptors();
    CheckOptions options;
    std::vector<bool> seen(descriptors.size(), false);

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

        const auto *descriptor = find_check_option(option);
        if (descriptor == nullptr)
            return CheckOptionsParseResult{
                make_error(CheckOptionErrorCode::unknown_option, option, index - 1)};
        if (descriptor->repeatable) {
            auto &values = options.*(descriptor->list_field);
            if (values.size() == max_check_takes)
                return CheckOptionsParseResult{
                    make_error(descriptor->list_field == &CheckOptions::takes
                                   ? CheckOptionErrorCode::too_many_takes
                                   : CheckOptionErrorCode::too_many_values,
                               option, index)};
            values.emplace_back(value);
            continue;
        }
        const auto slot = static_cast<std::size_t>(descriptor - descriptors.data());
        if (seen[slot])
            return CheckOptionsParseResult{
                make_error(CheckOptionErrorCode::duplicate_option, option, index - 1)};
        seen[slot] = true;
        (options.*(descriptor->text_field)).assign(value);
        options.given.emplace_back(descriptor->flag);
    }

    for (const auto &descriptor : descriptors) {
        if (!descriptor.required)
            continue;
        const bool missing = descriptor.repeatable ? (options.*(descriptor.list_field)).empty()
                                                   : (options.*(descriptor.text_field)).empty();
        if (missing)
            return CheckOptionsParseResult{make_error(CheckOptionErrorCode::missing_required_option,
                                                      descriptor.flag, arguments.size())};
    }

    for (const auto &descriptor : descriptors) {
        if (descriptor.kind != CheckOptionKind::choice)
            continue;
        const auto &value = options.*(descriptor.text_field);
        // An optional choice without a default may be absent (spec 002, --shaders).
        if (value.empty() && descriptor.default_value.empty())
            continue;
        if (std::find(descriptor.values.begin(), descriptor.values.end(), value) ==
            descriptor.values.end())
            return CheckOptionsParseResult{
                make_error(descriptor.invalid_value_error, descriptor.flag, arguments.size())};
    }

    for (const auto &descriptor : descriptors) {
        if (descriptor.kind == CheckOptionKind::unsigned_integer) {
            const auto &value = options.*(descriptor.text_field);
            if (!value.empty() && !valid_unsigned_32(value))
                return CheckOptionsParseResult{
                    make_error(descriptor.invalid_value_error, descriptor.flag, arguments.size())};
        } else if (descriptor.kind == CheckOptionKind::key_value) {
            for (const auto &value : options.*(descriptor.list_field)) {
                const auto equals = value.find('=');
                if (equals == std::string::npos || equals == 0U)
                    return CheckOptionsParseResult{make_error(descriptor.invalid_value_error,
                                                              descriptor.flag, arguments.size())};
            }
        }
    }
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
    case CheckOptionErrorCode::unsupported_shaders:
        return "unsupported_shaders";
    case CheckOptionErrorCode::invalid_unsigned_value:
        return "invalid_unsigned_value";
    case CheckOptionErrorCode::malformed_core_option:
        return "malformed_core_option";
    case CheckOptionErrorCode::too_many_values:
        return "too_many_values";
    }
    return "invalid_option";
}

} // namespace ayther::audio_qa
