#pragma once
//
// game_input.h — SDL keyboard + gamepad → normalized RetroPad state.
//
// The runtime owns the input *source* (SDL); the engine (AytherSession) owns the
// input *sink* (set_input → the core's input_state callback). This class is the
// translation: each frame it folds the live keyboard and the active gamepad into
// one Engine InputState.
//
// Gamepad-first by design (Ayther Play is a 10-foot UI): a controller is adopted
// as player 1 the moment it connects; the keyboard is always live as a fallback.
//
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <ayther/engine/input.hpp>

namespace ayther {

/// Owns the active SDL gamepad and maps live input to a RetroPad bit field.
class GameInput {
public:
    /// Load the optional controller mapping database and adopt a connected pad.
    GameInput();
    ~GameInput();

    GameInput(const GameInput&)            = delete;
    GameInput& operator=(const GameInput&) = delete;

    /// Process an SDL hot-plug event. Other event types are ignored.
    void handle_event(const SDL_Event& event);

    /// Poll keyboard and active-gamepad state for the current frame.
    /// @return The normalized joypad state consumed by AytherSession.
    engine::InputState poll() const;

    /// Return whether this object currently owns an open SDL gamepad handle.
    [[nodiscard]] bool has_gamepad() const noexcept {
        return gamepad_ != nullptr;
    }
    /// Return a borrowed SDL-owned name, or a stable fallback string.
    [[nodiscard]] const char* gamepad_name() const noexcept;

private:
    SDL_Gamepad* gamepad_{nullptr};  ///< Owned active controller (player 1).
    SDL_JoystickID gamepad_id_{};
};

}  // namespace ayther
