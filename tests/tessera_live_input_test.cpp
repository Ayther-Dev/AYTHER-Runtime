// tessera_live_input_test.cpp — spec 008 (RF-5.3, TP-304.2).
//
// The live input path of the Runtime (SDL gamepad → GameInput::poll →
// AytherSession::set_input → step) drives the Tessera provider. An SDL virtual
// gamepad stands in for the controller, so the test runs headless and with no
// hardware: it presses the default B (SDL_GAMEPAD_BUTTON_SOUTH) in frames
// 301-600 of FX-02. The program writes 0x2468 at VRAM 0xFFFC while B is held
// and 0x1357 otherwise, and opens PSG tone 0 when B goes down.

#include "game_input.h"
#include "native_session_config.h"

#include <SDL3/SDL.h>
#include <ayther/ayther_session.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(const bool condition, const std::string &what) {
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    }
}

std::optional<std::uint16_t> vram_word(const ayther::AytherSession &session,
                                       const std::uint32_t address) {
    size_t size = 0;
    const std::uint8_t *vram = session.video_ram(&size);
    if (!vram || size < address + 2)
        return std::nullopt;
    return static_cast<std::uint16_t>((vram[address ^ 1u] << 8) | vram[(address + 1) ^ 1u]);
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: tessera_live_input_tests <fx02/program.md>\n");
        return 2;
    }
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    // Only the virtual pad: a controller plugged into the machine running the
    // test would otherwise be adopted first.
    // (by name: not every SDL 3 build declares all of these macros).
    for (const char *hint :
         {"SDL_JOYSTICK_HIDAPI", "SDL_JOYSTICK_RAWINPUT", "SDL_XINPUT_ENABLED",
          "SDL_JOYSTICK_DIRECTINPUT", "SDL_JOYSTICK_WGI", "SDL_JOYSTICK_GAMEINPUT"})
        SDL_SetHint(hint, "0");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "AYTHER virtual pad";
    const SDL_JoystickID pad_id = SDL_AttachVirtualJoystick(&desc);
    SDL_Joystick *pad = pad_id ? SDL_OpenJoystick(pad_id) : nullptr;
    if (!pad) {
        std::fprintf(stderr, "virtual gamepad: %s\n", SDL_GetError());
        return 1;
    }
    SDL_UpdateJoysticks();

    // GameInput adopts the gamepad connected at startup, as in the Runtime.
    ayther::GameInput input;

    std::ifstream rom_file(argv[1], std::ios::binary);
    std::vector<std::uint8_t> rom{std::istreambuf_iterator<char>{rom_file},
                                  std::istreambuf_iterator<char>{}};
    auto prepared = ayther::runtime::megadrive_session_config(std::move(rom), {});
    if (!prepared.ok) {
        std::fprintf(stderr, "megadrive config: %s\n", prepared.message.c_str());
        return 1;
    }
    ayther::AytherSession::Config config;
    config.provider = ayther::EmulationProvider::tessera_native;
    config.native_config = std::move(prepared.config);
    config.enable_audio = false;
    config.derive_core_pack = false;
    auto created = ayther::AytherSession::create(config);
    if (!created) {
        std::fprintf(stderr, "session: %s\n", created.error.message.c_str());
        return 1;
    }
    auto session = std::move(*created);

    constexpr std::uint64_t pressed_first = 301, pressed_last = 600, frames = 900;
    std::uint32_t neutral = 0, pressed = 0, psg_on_press = 0;
    for (std::uint64_t frame = 1; frame <= frames; ++frame) {
        const bool hold = frame >= pressed_first && frame <= pressed_last;
        SDL_SetJoystickVirtualButton(pad, SDL_GAMEPAD_BUTTON_SOUTH, hold);
        SDL_UpdateJoysticks();
        SDL_UpdateGamepads();
        const auto state = input.poll();
        if (frame == pressed_first || frame == pressed_last + 1)
            check(state.pressed(ayther::engine::JoypadButton::b) == hold,
                  "GameInput did not follow the virtual B at frame " + std::to_string(frame));
        session->set_input(0, state);
        const ayther::FrameView &view = session->step();
        check(view.native_result.code == 0 && view.fb_pixels != nullptr,
              "frame " + std::to_string(frame) + " has no original");
        if (hold && frame <= pressed_first + 8)
            for (std::uint32_t i = 0; i < view.chip_write_count; ++i)
                psg_on_press += view.chip_writes[i].chip == 1 ? 1u : 0u;
        const bool neutral_window =
            (frame >= 100 && frame <= 200) || (frame >= 650 && frame <= 850);
        const bool pressed_window = frame >= 350 && frame <= 550;
        if (!neutral_window && !pressed_window)
            continue;
        const auto marker = vram_word(*session, 0xFFFC);
        if (neutral_window) {
            ++neutral;
            check(marker == std::uint16_t{0x1357},
                  "neutral marker at frame " + std::to_string(frame));
        } else {
            ++pressed;
            check(marker == std::uint16_t{0x2468},
                  "pressed marker at frame " + std::to_string(frame));
        }
    }
    check(neutral == 302 && pressed == 201, "window frame counts");
    check(psg_on_press > 0, "PSG tone did not open when B went down");

    session.reset();
    SDL_CloseJoystick(pad);
    SDL_DetachVirtualJoystick(pad_id);
    SDL_Quit();
    if (failures == 0)
        std::printf("tessera_live_input: FX-02 follows the live gamepad (B 301-600)\n");
    return failures == 0 ? 0 : 1;
}
