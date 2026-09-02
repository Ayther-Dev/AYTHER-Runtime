#pragma once

#include "ayther_runtime_version.h"

#include <ayther/engine/capabilities.hpp>

#include <string>
#include <string_view>

namespace ayther::runtime {

[[nodiscard]] inline std::string format_engine_version(
    const ayther::engine::Version version) {
    return std::to_string(version.major) + "." +
           std::to_string(version.minor) + "." +
           std::to_string(version.patch);
}

[[nodiscard]] inline std::string linked_engine_version() {
    return format_engine_version(ayther::engine::version());
}

[[nodiscard]] inline std::string version_report() {
    std::string result{"AYTHER Runtime "};
    result.append(runtime_version);
    result.append("; Engine ");
    result.append(linked_engine_version());
    return result;
}

[[nodiscard]] inline std::string window_title(const std::string_view mode) {
    std::string result{"AYTHER Runtime "};
    result.append(runtime_version);
    result.append(" — Engine ");
    result.append(linked_engine_version());
    result.append(" — ");
    result.append(mode);
    return result;
}

}  // namespace ayther::runtime
