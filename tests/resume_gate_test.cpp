// resume_gate_tests — the press that closes the pause menu stays out of the
// game.
//
// Pure folding over MappedInputState: no window, no SDL subsystem, no
// controller. The gate is armed with what is held when the menu closes and
// masks those inputs until each is released once.
#include "game_input.h"
#include "input_map.h"
#include <ayther/engine/input.hpp>
#include <cstddef>
#include <cstdio>

namespace {
int passed = 0;
int failed = 0;

void check(const bool condition, const char *description) {
    if (condition) {
        ++passed;
    } else {
        ++failed;
    }
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", description);
}

std::size_t gamepad_index_of(const ayther::InputMap &map,
                             const ayther::engine::JoypadButton action) {
    const auto bindings = map.gamepad_bindings();
    for (std::size_t index = 0; index < bindings.size(); ++index)
        if (bindings[index].action == action)
            return index;
    return 0;
}

std::size_t keyboard_index_of(const ayther::InputMap &map,
                              const ayther::engine::JoypadButton action) {
    const auto bindings = map.keyboard_bindings();
    for (std::size_t index = 0; index < bindings.size(); ++index)
        if (bindings[index].action == action)
            return index;
    return 0;
}
} // namespace

int main() {
    using ayther::engine::JoypadButton;
    const ayther::InputMap map = ayther::InputMap::defaults();
    const std::size_t south = gamepad_index_of(map, JoypadButton::b);
    const std::size_t east = gamepad_index_of(map, JoypadButton::a);
    const std::size_t start_key = keyboard_index_of(map, JoypadButton::start);

    std::puts("-- the button that closed the menu is masked until released");
    {
        ayther::ResumeGate gate;
        ayther::MappedInputState held;
        held.gamepad[south] = true; // A on «Continuar»
        gate.arm(held);
        check(gate.active(), "the gate is armed with the held button");

        const auto first = gate.filter(held);
        check(!first.gamepad[south], "the held button does not reach the game");
        check(!ayther::compose_input_state(map, first).pressed(JoypadButton::b),
              "the RetroPad bit stays clear while it is held");

        ayther::MappedInputState with_other = held;
        with_other.gamepad[east] = true;
        const auto second = gate.filter(with_other);
        check(!second.gamepad[south] && second.gamepad[east],
              "a button pressed after the resume passes while the old one is masked");

        ayther::MappedInputState released;
        const auto third = gate.filter(released);
        check(!third.gamepad[south], "release frame: nothing pressed");
        check(!gate.active(), "releasing the button disarms the gate");

        const auto fourth = gate.filter(held);
        check(fourth.gamepad[south], "the next press is the player's and passes");
        check(ayther::compose_input_state(map, fourth).pressed(JoypadButton::b),
              "the RetroPad bit is set again");
    }

    std::puts("-- keyboard and d-pad are gated the same way");
    {
        ayther::ResumeGate gate;
        ayther::MappedInputState held;
        held.keyboard[start_key] = true;
        held.dpad_left = true;
        gate.arm(held);
        const auto filtered = gate.filter(held);
        check(!filtered.keyboard[start_key], "a held keyboard binding is masked");
        check(!filtered.dpad_left, "a held d-pad direction is masked");
        check(!ayther::compose_input_state(map, filtered).pressed(JoypadButton::start),
              "Start does not reach the game");
        ayther::MappedInputState released;
        (void)gate.filter(released);
        check(!gate.active(), "releasing everything disarms the gate");
    }

    std::puts("-- nothing held at the resume: the gate is inert");
    {
        ayther::ResumeGate gate;
        gate.arm(ayther::MappedInputState{});
        check(!gate.active(), "no mask");
        ayther::MappedInputState press;
        press.gamepad[south] = true;
        press.left_x = -30000;
        const auto filtered = gate.filter(press);
        check(filtered.gamepad[south] && filtered.left_x == -30000,
              "every input passes through unchanged");
    }

    std::puts("-- the stick is analog and never masked");
    {
        ayther::ResumeGate gate;
        ayther::MappedInputState held;
        held.left_y = -30000;
        gate.arm(held);
        check(!gate.active(), "a tilted stick arms nothing");
        check(gate.filter(held).left_y == -30000, "the stick passes through");
    }

    std::printf("resume_gate_tests: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
