#pragma once

#include "capabilities.h"

#include <string_view>
#include <variant>

namespace ayther::audio_qa {

enum class CapabilityReportError {
    marker_missing,
    duplicate_report,
    malformed_report,
    incompatible_schema,
};

using CapabilityReportResult = std::variant<CapabilitySet, CapabilityReportError>;

[[nodiscard]] CapabilityReportResult
decode_runtime_capability_report(std::string_view process_output);

} // namespace ayther::audio_qa
