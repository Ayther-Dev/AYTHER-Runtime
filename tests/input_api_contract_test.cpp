#include "game_input.h"

#include <ayther/engine/input.hpp>

#include <cstdint>
#include <type_traits>
#include <utility>

using ayther::engine::InputState;
using ayther::engine::JoypadButton;

constexpr InputState kDirections =
    InputState{JoypadButton::up} | InputState{JoypadButton::right};

static_assert(std::is_trivially_copyable_v<InputState>);
static_assert(std::is_same_v<
              decltype(std::declval<const ayther::GameInput&>().poll()),
              InputState>);
static_assert(kDirections.pressed(JoypadButton::up));
static_assert(kDirections.pressed(JoypadButton::right));
static_assert(!kDirections.pressed(JoypadButton::down));
static_assert(kDirections.bits() ==
              ((std::uint16_t{1} << 4) | (std::uint16_t{1} << 7)));

int main() {
    return 0;
}
