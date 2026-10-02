#pragma once

#include <string>

namespace ayther::runtime {

inline constexpr const char *qa_capabilities_marker = "AYTHER_QA_CAPABILITIES ";

[[nodiscard]] std::string qa_capabilities_report();

} // namespace ayther::runtime
