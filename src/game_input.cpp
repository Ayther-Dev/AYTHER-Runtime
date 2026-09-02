#include "game_input.h"

#include <cstdint>
#include <cstdio>
#include <SDL3/SDL_keyboard.h>

namespace ayther {

namespace {
void set_button(engine::InputState& input, engine::JoypadButton button,
                bool is_pressed) {
    if (is_pressed) {
        input = input | engine::InputState{button};
    }
}
}  // namespace

GameInput::GameInput() {
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

    engine::InputState input;

    // --- Keyboard (always live) ----------------------------------------------
    // Z = jump (Genesis A/B/C all jump in Sonic); arrows = D-pad; Enter = Start.
    if (const bool* keyboard_state = SDL_GetKeyboardState(nullptr)) {
        set_button(input, JoypadButton::up, keyboard_state[SDL_SCANCODE_UP]);
        set_button(input, JoypadButton::down,
                   keyboard_state[SDL_SCANCODE_DOWN]);
        set_button(input, JoypadButton::left,
                   keyboard_state[SDL_SCANCODE_LEFT]);
        set_button(input, JoypadButton::right,
                   keyboard_state[SDL_SCANCODE_RIGHT]);
        set_button(input, JoypadButton::b, keyboard_state[SDL_SCANCODE_Z]);
        set_button(input, JoypadButton::a, keyboard_state[SDL_SCANCODE_X]);
        set_button(input, JoypadButton::y, keyboard_state[SDL_SCANCODE_A]);
        set_button(input, JoypadButton::x, keyboard_state[SDL_SCANCODE_S]);
        set_button(input, JoypadButton::l, keyboard_state[SDL_SCANCODE_Q]);
        set_button(input, JoypadButton::r, keyboard_state[SDL_SCANCODE_W]);
        set_button(input, JoypadButton::start,
                   keyboard_state[SDL_SCANCODE_RETURN]);
        set_button(input, JoypadButton::select,
                   keyboard_state[SDL_SCANCODE_RSHIFT] ||
                       keyboard_state[SDL_SCANCODE_BACKSPACE]);
    }

    // --- Gamepad (OR-ed in) --------------------------------------------------
    if (gamepad_) {
        const auto is_gamepad_button_pressed =
            [this](SDL_GamepadButton button) {
                return SDL_GetGamepadButton(gamepad_, button);
            };
        set_button(input, JoypadButton::up,
                   is_gamepad_button_pressed(SDL_GAMEPAD_BUTTON_DPAD_UP));
        set_button(input, JoypadButton::down,
                   is_gamepad_button_pressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
        set_button(input, JoypadButton::left,
                   is_gamepad_button_pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
        set_button(input, JoypadButton::right,
                   is_gamepad_button_pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
        set_button(input, JoypadButton::b,
                   is_gamepad_button_pressed(SDL_GAMEPAD_BUTTON_SOUTH));
        set_button(input, JoypadButton::a,
                   is_gamepad_button_pressed(SDL_GAMEPAD_BUTTON_EAST));
        set_button(input, JoypadButton::y,
                   is_gamepad_button_pressed(SDL_GAMEPAD_BUTTON_WEST));
        set_button(input, JoypadButton::x,
                   is_gamepad_button_pressed(SDL_GAMEPAD_BUTTON_NORTH));
        set_button(input, JoypadButton::l,
                   is_gamepad_button_pressed(
                       SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
        set_button(input, JoypadButton::r,
                   is_gamepad_button_pressed(
                       SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
        set_button(input, JoypadButton::start,
                   is_gamepad_button_pressed(SDL_GAMEPAD_BUTTON_START));
        set_button(input, JoypadButton::select,
                   is_gamepad_button_pressed(SDL_GAMEPAD_BUTTON_BACK));

        // Left analog stick → D-pad (≈ 50% deadzone).
        constexpr std::int16_t axis_deadzone = 16000;
        const std::int16_t axis_x =
            SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX);
        const std::int16_t axis_y =
            SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY);
        if (axis_x < -axis_deadzone) {
            set_button(input, JoypadButton::left, true);
        }
        if (axis_x > axis_deadzone) {
            set_button(input, JoypadButton::right, true);
        }
        if (axis_y < -axis_deadzone) {
            set_button(input, JoypadButton::up, true);
        }
        if (axis_y > axis_deadzone) {
            set_button(input, JoypadButton::down, true);
        }
    }

    return input;
}

}  // namespace ayther
