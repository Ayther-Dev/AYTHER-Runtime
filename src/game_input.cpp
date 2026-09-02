#include "game_input.h"
#include "libretro_host/libretro.h"   // RETRO_DEVICE_ID_JOYPAD_* (via ayther_engine)

#include <cstdint>
#include <cstdio>
#include <SDL3/SDL_keyboard.h>

namespace ayther {

namespace {
inline void set_bit(uint16_t& b, int id, bool on) {
    if (on) b |= static_cast<uint16_t>(1u << id);
}
}  // namespace

GameInput::GameInput() {
    // Community controller DB (optional). SDL3 ships a large built-in mapping DB,
    // so this only *augments* it for exotic pads; absence is not an error.
    if (SDL_AddGamepadMappingsFromFile("gamecontrollerdb.txt") < 0) {
        std::fprintf(stdout, "[input] no gamecontrollerdb.txt (using SDL built-in mappings)\n");
    }
    // Adopt a gamepad already connected at startup.
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        if (count > 0) { pad_ = SDL_OpenGamepad(ids[0]); pad_id_ = ids[0]; }
        SDL_free(ids);
    }
    std::fprintf(stdout, "[input] keyboard ready%s%s\n",
                 pad_ ? " + gamepad: " : " (no gamepad)",
                 pad_ ? gamepad_name() : "");
}

GameInput::~GameInput() {
    if (pad_) SDL_CloseGamepad(pad_);
}

const char* GameInput::gamepad_name() const {
    const char* n = pad_ ? SDL_GetGamepadName(pad_) : nullptr;
    return n ? n : "(unknown)";
}

void GameInput::handle_event(const SDL_Event& e) {
    if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
        if (!pad_) {                          // adopt the first as player 1
            pad_    = SDL_OpenGamepad(e.gdevice.which);
            pad_id_ = e.gdevice.which;
            if (pad_) std::fprintf(stdout, "[input] gamepad connected: %s\n", gamepad_name());
        }
    } else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
        if (pad_ && e.gdevice.which == pad_id_) {
            std::fprintf(stdout, "[input] gamepad disconnected\n");
            SDL_CloseGamepad(pad_);
            pad_ = nullptr;
            pad_id_ = 0;
        }
    }
}

uint16_t GameInput::poll() const {
    uint16_t b = 0;

    // --- Keyboard (always live) ----------------------------------------------
    // Z = jump (Genesis A/B/C all jump in Sonic); arrows = D-pad; Enter = Start.
    if (const bool* k = SDL_GetKeyboardState(nullptr)) {
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_UP,     k[SDL_SCANCODE_UP]);
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_DOWN,   k[SDL_SCANCODE_DOWN]);
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_LEFT,   k[SDL_SCANCODE_LEFT]);
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_RIGHT,  k[SDL_SCANCODE_RIGHT]);
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_B,      k[SDL_SCANCODE_Z]);
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_A,      k[SDL_SCANCODE_X]);
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_Y,      k[SDL_SCANCODE_A]);
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_X,      k[SDL_SCANCODE_S]);
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_L,      k[SDL_SCANCODE_Q]);
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_R,      k[SDL_SCANCODE_W]);
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_START,  k[SDL_SCANCODE_RETURN]);
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_SELECT, k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_BACKSPACE]);
    }

    // --- Gamepad (OR-ed in) --------------------------------------------------
    if (pad_) {
        auto pb = [&](SDL_GamepadButton sb) { return SDL_GetGamepadButton(pad_, sb); };
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_UP,     pb(SDL_GAMEPAD_BUTTON_DPAD_UP));
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_DOWN,   pb(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_LEFT,   pb(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_RIGHT,  pb(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_B,      pb(SDL_GAMEPAD_BUTTON_SOUTH));   // jump
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_A,      pb(SDL_GAMEPAD_BUTTON_EAST));
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_Y,      pb(SDL_GAMEPAD_BUTTON_WEST));
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_X,      pb(SDL_GAMEPAD_BUTTON_NORTH));
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_L,      pb(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_R,      pb(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_START,  pb(SDL_GAMEPAD_BUTTON_START));
        set_bit(b, RETRO_DEVICE_ID_JOYPAD_SELECT, pb(SDL_GAMEPAD_BUTTON_BACK));

        // Left analog stick → D-pad (≈ 50% deadzone).
        constexpr int16_t dz = 16000;
        const int16_t ax = SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_LEFTX);
        const int16_t ay = SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_LEFTY);
        if (ax < -dz) set_bit(b, RETRO_DEVICE_ID_JOYPAD_LEFT,  true);
        if (ax >  dz) set_bit(b, RETRO_DEVICE_ID_JOYPAD_RIGHT, true);
        if (ay < -dz) set_bit(b, RETRO_DEVICE_ID_JOYPAD_UP,    true);
        if (ay >  dz) set_bit(b, RETRO_DEVICE_ID_JOYPAD_DOWN,  true);
    }

    return b;
}

}  // namespace ayther
