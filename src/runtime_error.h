#pragma once

#include <string_view>

namespace ayther::runtime {

// Stable process exit contract. Values already consumed by Play keep their
// historical numbers; new categories use sysexits-compatible values.
enum class RuntimeExitCode : int {
    success = 0,
    startup_failed = 1,
    core_load_failed = 2,
    invalid_core = 3,
    cli_usage = 64,
    protocol_incompatible = 65,
    service_unavailable = 69,
    io_failure = 74,
    configuration_invalid = 78,
};

[[nodiscard]] constexpr int exit_code(const RuntimeExitCode code) noexcept {
    return static_cast<int>(code);
}

enum class ErrorDomain {
    cli,
    core,
    pack,
    state,
    vulkan,
    persistence,
    protocol,
    input,
};

enum class ErrorSeverity {
    warning,
    recoverable,
    fatal,
};

// These identifiers are part of the Runtime--Play machine contract. Human
// messages may change or be translated without changing these values.
enum class RuntimeErrorCode {
    cli_invalid_argument,
    core_load_failed,
    core_invalid,
    pack_rejected,
    pack_no_active_subsystems,
    state_restore_failed,
    state_save_failed,
    vulkan_unavailable,
    vulkan_initialization_failed,
    vulkan_frame_failed,
    postprocess_degraded,
    config_invalid,
    persistence_io_failed,
    capture_failed,
    protocol_incompatible,
    input_map_invalid,
};

[[nodiscard]] constexpr std::string_view
error_reason(const RuntimeErrorCode code) noexcept {
    switch (code) {
    case RuntimeErrorCode::cli_invalid_argument:
        return "cli.invalid_argument";
    case RuntimeErrorCode::core_load_failed:
        return "core.load_failed";
    case RuntimeErrorCode::core_invalid:
        return "core.invalid";
    case RuntimeErrorCode::pack_rejected:
        return "pack.rejected";
    case RuntimeErrorCode::pack_no_active_subsystems:
        return "pack.no_active_subsystems";
    case RuntimeErrorCode::state_restore_failed:
        return "state.restore_failed";
    case RuntimeErrorCode::state_save_failed:
        return "state.save_failed";
    case RuntimeErrorCode::vulkan_unavailable:
        return "vulkan.unavailable";
    case RuntimeErrorCode::vulkan_initialization_failed:
        return "vulkan.initialization_failed";
    case RuntimeErrorCode::vulkan_frame_failed:
        return "vulkan.frame_failed";
    case RuntimeErrorCode::postprocess_degraded:
        return "vulkan.postprocess_degraded";
    case RuntimeErrorCode::config_invalid:
        return "persistence.config_invalid";
    case RuntimeErrorCode::persistence_io_failed:
        return "persistence.io_failed";
    case RuntimeErrorCode::capture_failed:
        return "persistence.capture_failed";
    case RuntimeErrorCode::protocol_incompatible:
        return "protocol.incompatible";
    case RuntimeErrorCode::input_map_invalid:
        return "input.map_invalid";
    }
    return "runtime.unknown";
}

[[nodiscard]] constexpr ErrorDomain
error_domain(const RuntimeErrorCode code) noexcept {
    switch (code) {
    case RuntimeErrorCode::cli_invalid_argument:
        return ErrorDomain::cli;
    case RuntimeErrorCode::core_load_failed:
    case RuntimeErrorCode::core_invalid:
        return ErrorDomain::core;
    case RuntimeErrorCode::pack_rejected:
    case RuntimeErrorCode::pack_no_active_subsystems:
        return ErrorDomain::pack;
    case RuntimeErrorCode::state_restore_failed:
    case RuntimeErrorCode::state_save_failed:
        return ErrorDomain::state;
    case RuntimeErrorCode::vulkan_unavailable:
    case RuntimeErrorCode::vulkan_initialization_failed:
    case RuntimeErrorCode::vulkan_frame_failed:
    case RuntimeErrorCode::postprocess_degraded:
        return ErrorDomain::vulkan;
    case RuntimeErrorCode::config_invalid:
    case RuntimeErrorCode::persistence_io_failed:
    case RuntimeErrorCode::capture_failed:
        return ErrorDomain::persistence;
    case RuntimeErrorCode::protocol_incompatible:
        return ErrorDomain::protocol;
    case RuntimeErrorCode::input_map_invalid:
        return ErrorDomain::input;
    }
    return ErrorDomain::protocol;
}

[[nodiscard]] constexpr ErrorSeverity
error_severity(const RuntimeErrorCode code) noexcept {
    switch (code) {
    case RuntimeErrorCode::pack_no_active_subsystems:
    case RuntimeErrorCode::postprocess_degraded:
        return ErrorSeverity::warning;
    case RuntimeErrorCode::pack_rejected:
    case RuntimeErrorCode::state_restore_failed:
    case RuntimeErrorCode::state_save_failed:
    case RuntimeErrorCode::vulkan_unavailable:
    case RuntimeErrorCode::vulkan_initialization_failed:
    case RuntimeErrorCode::vulkan_frame_failed:
    case RuntimeErrorCode::config_invalid:
    case RuntimeErrorCode::persistence_io_failed:
    case RuntimeErrorCode::capture_failed:
        return ErrorSeverity::recoverable;
    case RuntimeErrorCode::input_map_invalid:
        return ErrorSeverity::fatal;
    default:
        return ErrorSeverity::fatal;
    }
}

[[nodiscard]] constexpr bool
is_recoverable(const RuntimeErrorCode code) noexcept {
    return error_severity(code) != ErrorSeverity::fatal;
}

}  // namespace ayther::runtime
