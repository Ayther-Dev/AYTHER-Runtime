#pragma once

#include "check_options.h"

#include <string>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

struct CheckProfile {
    std::string profile_id;
    std::string primary_take;
    std::string complementary_take;
};

enum class TakeSelectionSource { explicit_options, profile_primary };

struct TakeSelection {
    TakeSelectionSource source{TakeSelectionSource::explicit_options};
    std::vector<std::string> takes;
};

enum class TakeSelectionError { missing_profile_primary_take };

using TakeSelectionResult = std::variant<TakeSelection, TakeSelectionError>;

[[nodiscard]] CheckProfile golden_axe_check_profile();
[[nodiscard]] TakeSelectionResult select_check_takes(const CheckOptions &options,
                                                     const CheckProfile &profile);

} // namespace ayther::audio_qa
