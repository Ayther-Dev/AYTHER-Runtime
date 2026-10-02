#pragma once

#include "initial_state.h"

#include <string_view>

namespace ayther::audio_qa {

enum class HdStateAvailability { absent, supplied };

struct HdInitializationOperationResult {
    bool succeeded{};
    std::string_view code;
    std::string_view detail;
};

using HdInitializationOperation = HdInitializationOperationResult (*)(void *context) noexcept;

struct HdInitializationOperations {
    void *context{};
    HdInitializationOperation restore_supplied{};
    HdInitializationOperation prepare_fresh{};
};

struct HdInitializationOperationRecord {
    bool attempted{};
    bool succeeded{};
    std::string_view code{"not_attempted"};
    std::string_view detail;
};

struct HdInitializationSelection {
    HdInitialization initialization{HdInitialization::unknown};
    RestoreResult restore_result{RestoreResult::not_attempted};
    std::string_view reason{"hd_initialization_not_attempted"};
    HdInitializationOperationRecord restore;
    HdInitializationOperationRecord fresh;
    bool evidence_incomplete{true};
};

[[nodiscard]] HdInitializationSelection
select_hd_initialization(HdStateAvailability availability,
                         const HdInitializationOperations &operations) noexcept;

} // namespace ayther::audio_qa
