#include "runtime_error.h"

#include <string_view>

using namespace std::literals;

static_assert(ayther::runtime::exit_code(
                  ayther::runtime::RuntimeExitCode::success) == 0);
static_assert(ayther::runtime::exit_code(
                  ayther::runtime::RuntimeExitCode::startup_failed) == 1);
static_assert(ayther::runtime::exit_code(
                  ayther::runtime::RuntimeExitCode::core_load_failed) == 2);
static_assert(ayther::runtime::exit_code(
                  ayther::runtime::RuntimeExitCode::invalid_core) == 3);
static_assert(ayther::runtime::exit_code(
                  ayther::runtime::RuntimeExitCode::cli_usage) == 64);
static_assert(ayther::runtime::exit_code(
                  ayther::runtime::RuntimeExitCode::protocol_incompatible) ==
              65);
static_assert(ayther::runtime::exit_code(
                  ayther::runtime::RuntimeExitCode::service_unavailable) ==
              69);
static_assert(ayther::runtime::exit_code(
                  ayther::runtime::RuntimeExitCode::io_failure) == 74);

static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::cli_invalid_argument) ==
              "cli.invalid_argument"sv);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::core_load_failed) ==
              "core.load_failed"sv);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::core_invalid) ==
              "core.invalid"sv);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::pack_rejected) ==
              "pack.rejected"sv);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::pack_no_active_subsystems) ==
              "pack.no_active_subsystems"sv);
static_assert(ayther::runtime::exit_code(
                  ayther::runtime::RuntimeExitCode::configuration_invalid) == 78);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::input_map_invalid) ==
              "input.map_invalid"sv);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::state_restore_failed) ==
              "state.restore_failed"sv);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::state_save_failed) ==
              "state.save_failed"sv);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::vulkan_unavailable) ==
              "vulkan.unavailable"sv);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::vulkan_initialization_failed) ==
              "vulkan.initialization_failed"sv);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::vulkan_frame_failed) ==
              "vulkan.frame_failed"sv);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::postprocess_degraded) ==
              "vulkan.postprocess_degraded"sv);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::config_invalid) ==
              "persistence.config_invalid"sv);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::persistence_io_failed) ==
              "persistence.io_failed"sv);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::capture_failed) ==
              "persistence.capture_failed"sv);
static_assert(ayther::runtime::error_reason(
                  ayther::runtime::RuntimeErrorCode::protocol_incompatible) ==
              "protocol.incompatible"sv);
static_assert(ayther::runtime::is_recoverable(
    ayther::runtime::RuntimeErrorCode::postprocess_degraded));
static_assert(ayther::runtime::is_recoverable(
    ayther::runtime::RuntimeErrorCode::state_save_failed));
static_assert(!ayther::runtime::is_recoverable(
    ayther::runtime::RuntimeErrorCode::core_invalid));
static_assert(!ayther::runtime::is_recoverable(
    ayther::runtime::RuntimeErrorCode::protocol_incompatible));
static_assert(!ayther::runtime::is_recoverable(
    ayther::runtime::RuntimeErrorCode::input_map_invalid));

int main() { return 0; }
