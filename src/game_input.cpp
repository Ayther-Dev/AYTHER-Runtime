#include "game_input.h"

#include <cstdint>
#include <cstdio>
#include <SDL3/SDL_keyboard.h>
#include <utility>

namespace ayther {

namespace {
void set_button(engine::InputState& input, engine::JoypadButton button,
                bool is_pressed) {
    if (is_pressed) {
        input = input | engine::InputState{button};
    }
}
}  // namespace

engine::InputState compose_input_state(const InputMap& map,
                                       const MappedInputState& state) noexcept {
    engine::InputState input;
    const auto keyboard = map.keyboard_bindings();
    for (std::size_t index = 0; index < keyboard.size(); ++index) {
        set_button(input, keyboard[index].action, state.keyboard[index]);
    }

    const auto gamepad = map.gamepad_bindings();
    for (std::size_t index = 0; index < gamepad.size(); ++index) {
        set_button(input, gamepad[index].action, state.gamepad[index]);
    }

    using engine::JoypadButton;
    set_button(input, JoypadButton::up, state.dpad_up);
    set_button(input, JoypadButton::down, state.dpad_down);
    set_button(input, JoypadButton::left, state.dpad_left);
    set_button(input, JoypadButton::right, state.dpad_right);

    constexpr std::int16_t axis_deadzone = 16000;
    set_button(input, JoypadButton::left, state.left_x < -axis_deadzone);
    set_button(input, JoypadButton::right, state.left_x > axis_deadzone);
    set_button(input, JoypadButton::up, state.left_y < -axis_deadzone);
    set_button(input, JoypadButton::down, state.left_y > axis_deadzone);
    return input;
}

GameInput::GameInput(InputMap input_map) : input_map_(std::move(input_map)) {
    // Community controller DB (optional). SDL3 ships a large built-in mapping DB,
    // so this only *augments* it for exotic pads; absence is not an error.
    if (SDL_AddGamepadMappingsFromFile("gamecontrollerdb.txt") < 0) {
        std::fprintf(stdout, "[input] no gamecontrollerdb.txt (using SDL built-in mappings)\n");
    }
    // Adopt a gamepad already connected at startup.
    int gamepad_count = 0;
    if (SDL_JoystickID* gamepad_ids = SDL_GetGamepads(&gamepad_count)) {
        if (gamepad_count > 0) {
            gamepad_ = SDL_OpenGamepad(gamepad_ids[0]);
            gamepad_id_ = gamepad_ids[0];
        }
        SDL_free(gamepad_ids);
    }
    std::fprintf(stdout, "[input] keyboard ready%s%s\n",
                 gamepad_ ? " + gamepad: " : " (no gamepad)",
                 gamepad_ ? gamepad_name() : "");
}

GameInput::~GameInput() {
    if (gamepad_) {
        SDL_CloseGamepad(gamepad_);
    }
}

const char* GameInput::gamepad_name() const noexcept {
    const char* name = gamepad_ ? SDL_GetGamepadName(gamepad_) : nullptr;
    return name ? name : "(unknown)";
}

void GameInput::handle_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        if (!gamepad_) {  // adopt the first as player 1
            gamepad_ = SDL_OpenGamepad(event.gdevice.which);
            gamepad_id_ = event.gdevice.which;
            if (gamepad_) {
                std::fprintf(stdout, "[input] gamepad connected: %s\n",
                             gamepad_name());
            }
        }
    } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        if (gamepad_ && event.gdevice.which == gamepad_id_) {
            std::fprintf(stdout, "[input] gamepad disconnected\n");
            SDL_CloseGamepad(gamepad_);
            gamepad_ = nullptr;
            gamepad_id_ = 0;
        }
    }
}

engine::InputState GameInput::poll() const {
    using engine::JoypadButton;

    MappedInputState state;

    // --- Keyboard (always live) ----------------------------------------------
    if (const bool* keyboard_state = SDL_GetKeyboardState(nullptr)) {
        const auto bindings = input_map_.keyboard_bindings();
        for (std::size_t index = 0; index < bindings.size(); ++index) {
            state.keyboard[index] = keyboard_state[bindings[index].scancode];
            if (input_map_.legacy_select_backspace() &&
                bindings[index].action == JoypadButton::select) {
                state.keyboard[index] = state.keyboard[index] ||
                                        keyboard_state[SDL_SCANCODE_BACKSPACE];
            }
        }
    }

    // --- Gamepad (OR-ed in) --------------------------------------------------
    if (gamepad_) {
        const auto bindings = input_map_.gamepad_bindings();
        for (std::size_t index = 0; index < bindings.size(); ++index) {
            state.gamepad[index] =
                SDL_GetGamepadButton(gamepad_, bindings[index].button);
        }
        state.dpad_up =
            SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_UP);
        state.dpad_down =
            SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        state.dpad_left =
            SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        state.dpad_right =
            SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        state.left_x = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX);
        state.left_y = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY);
    }

    return compose_input_state(input_map_, state);
}

}  // namespace ayther
