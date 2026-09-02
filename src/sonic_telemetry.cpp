#include "sonic_telemetry.h"

#include <cstddef>

namespace ayther::runtime {
namespace {

constexpr std::size_t kPositionXOffset{0xB008U};
constexpr std::size_t kPositionYOffset{0xB00CU};
constexpr std::size_t kVelocityXOffset{0xB014U};
constexpr std::size_t kVelocityYOffset{0xB018U};

[[nodiscard]] std::int16_t read_i16_be(const std::span<const std::uint8_t> bytes,
                                       const std::size_t offset) noexcept {
    const auto value =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                   static_cast<std::uint16_t>(bytes[offset + 1U]));
    return static_cast<std::int16_t>(value);
}

}  // namespace

SonicTelemetry decode_sonic_telemetry(const std::span<const std::uint8_t> work_ram) noexcept {
    SonicTelemetry telemetry;
    if (work_ram.size() >= kPositionYOffset + 2U) {
        telemetry.x = read_i16_be(work_ram, kPositionXOffset);
        telemetry.y = read_i16_be(work_ram, kPositionYOffset);
        telemetry.has_position = true;
    }
    if (work_ram.size() >= kVelocityYOffset + 2U) {
        telemetry.velocity_x = read_i16_be(work_ram, kVelocityXOffset);
        telemetry.velocity_y = read_i16_be(work_ram, kVelocityYOffset);
        telemetry.has_velocity = true;
    }
    return telemetry;
}

}  // namespace ayther::runtime
