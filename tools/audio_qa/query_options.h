#pragma once

#include "check_options.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace ayther::audio_qa {

struct QueryOptions {
    std::string index;
    std::string run_id;
    std::string producer_id;
    std::uint64_t sequence{};
    std::string language = "es";
};

using QueryOptionsParseResult = std::variant<QueryOptions, CheckOptionError>;

[[nodiscard]] QueryOptionsParseResult
parse_query_options(std::span<const std::string_view> arguments);

} // namespace ayther::audio_qa
