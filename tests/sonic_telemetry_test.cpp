#include "sonic_telemetry.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", message);
    if (!condition) {
        ++failures;
    }
}

}  // namespace

int main() {
    check(!ayther::runtime::decode_sonic_telemetry({}).has_position,
          "an empty RAM view has no position");

    std::array<std::uint8_t, 0xB01AU> ram{};
    ram[0xB008U] = 0x12U;
    ram[0xB009U] = 0x34U;
    ram[0xB00CU] = 0xFFU;
    ram[0xB00DU] = 0xFEU;
    ram[0xB014U] = 0x80U;
    ram[0xB015U] = 0x00U;
    ram[0xB018U] = 0x00U;
    ram[0xB019U] = 0x7FU;

    const auto telemetry = ayther::runtime::decode_sonic_telemetry(ram);
    check(telemetry.has_position && telemetry.x == 0x1234 && telemetry.y == -2,
          "position is decoded as signed big-endian values");
    check(telemetry.has_velocity && telemetry.velocity_x == -32768 && telemetry.velocity_y == 127,
          "velocity is decoded as signed big-endian values");

    std::printf("%s\n", failures == 0 ? "=== PASS ===" : "=== FAIL ===");
    return failures == 0 ? 0 : 1;
}
