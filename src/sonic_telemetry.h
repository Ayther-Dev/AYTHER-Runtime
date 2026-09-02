#pragma once

#include <cstdint>
#include <span>

namespace ayther::runtime {

/// Runtime-only interpretation of Sonic 2's work-RAM diagnostics.
///
/// This is presentation telemetry, not an Engine/core contract. The decoder
/// accepts a bounded view and never reads beyond it.
struct SonicTelemetry {
    std::int16_t x{};
    std::int16_t y{};
    std::int16_t velocity_x{};
    std::int16_t velocity_y{};
    bool has_position{};
    bool has_velocity{};
};

[[nodiscard]] SonicTelemetry
decode_sonic_telemetry(std::span<const std::uint8_t> work_ram) noexcept;

}  // namespace ayther::runtime
