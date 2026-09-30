#include "query_options.h"

#include "model_limits.h"

#include <array>
#include <charconv>
#include <utility>

namespace ayther::audio_qa {
namespace {

CheckOptionError error(CheckOptionErrorCode code, std::string_view option, std::size_t index) {
    return {code, std::string{option}, index};
}

} // namespace

QueryOptionsParseResult parse_query_options(std::span<const std::string_view> arguments) {
    if (arguments.size() > 10)
        return error(CheckOptionErrorCode::too_many_arguments, {}, arguments.size());
    QueryOptions options;
    std::array<bool, 5> seen{};
    std::string sequence_text;
    for (std::size_t index{}; index < arguments.size(); ++index) {
        const auto option = arguments[index];
        if (!option.starts_with("--"))
            return error(CheckOptionErrorCode::unknown_option, option, index);
        if (index + 1 >= arguments.size() || arguments[index + 1].starts_with("--"))
            return error(CheckOptionErrorCode::missing_value, option, index);
        const auto value = arguments[++index];
        if (value.empty())
            return error(CheckOptionErrorCode::empty_value, option, index);
        if (value.size() > max_check_option_value_bytes)
            return error(CheckOptionErrorCode::value_too_long, option, index);

        std::string *destination{};
        std::size_t slot{};
        if (option == "--index") {
            destination = &options.index;
        } else if (option == "--run-id") {
            destination = &options.run_id;
            slot = 1;
        } else if (option == "--producer-id") {
            destination = &options.producer_id;
            slot = 2;
        } else if (option == "--sequence") {
            destination = &sequence_text;
            slot = 3;
        } else if (option == "--language") {
            destination = &options.language;
            slot = 4;
        } else {
            return error(CheckOptionErrorCode::unknown_option, option, index - 1);
        }
        if (seen[slot])
            return error(CheckOptionErrorCode::duplicate_option, option, index - 1);
        seen[slot] = true;
        destination->assign(value);
    }

    const std::array required{
        std::pair{std::string_view{"--index"}, options.index.empty()},
        std::pair{std::string_view{"--run-id"}, options.run_id.empty()},
        std::pair{std::string_view{"--producer-id"}, options.producer_id.empty()},
        std::pair{std::string_view{"--sequence"}, sequence_text.empty()},
    };
    for (const auto &[option, missing] : required)
        if (missing)
            return error(CheckOptionErrorCode::missing_required_option, option, arguments.size());
    if (options.run_id.size() > max_identity_bytes ||
        options.producer_id.size() > max_identity_bytes)
        return error(CheckOptionErrorCode::value_too_long, "identity", arguments.size());
    const auto parsed = std::from_chars(
        sequence_text.data(), sequence_text.data() + sequence_text.size(), options.sequence);
    if (parsed.ec != std::errc{} || parsed.ptr != sequence_text.data() + sequence_text.size() ||
        options.sequence == 0)
        return error(CheckOptionErrorCode::invalid_number, "--sequence", arguments.size());
    if (options.language != "es" && options.language != "en")
        return error(CheckOptionErrorCode::unsupported_language, "--language", arguments.size());
    return options;
}

} // namespace ayther::audio_qa
