#pragma once

#include "debug_view.h"
#include "key_router.h"

#include <SDL3/SDL.h>

#include <optional>

namespace ayther::runtime {

// Spec 002, plan §5.5 and §5.10 (RF-4.5, RF-4.7, RF-4.9, RF-2.6, RF-4.11): what the replay
// window reports to the inspection. Keys are only Space, arrows and I (everything else is
// `other`); nothing of the keyboard or the gamepad reaches the game.
struct PresentationEvent {
    enum class Kind {
        key_down,
        key_up,
        focus,
        minimized,
        audio_removed,
        audio_added,
        close,
        scroll
    };
    Kind kind{Kind::close};
    replay_inspection::Key key{replay_inspection::Key::other};
    bool repeat{};
    // focus: gained; minimized: minimized; audio_removed: the device in use.
    bool value{};
    // D15: the overlay scrolls with the wheel and Page Up, Page Down, Home and End.
    replay_inspection::ScrollKey scroll{replay_inspection::ScrollKey::home};
};

// The event for the inspection, if the SDL event is one; `own` is the replay window.
[[nodiscard]] std::optional<PresentationEvent> translate_window_event(const SDL_Event &event,
                                                                      SDL_WindowID own) noexcept;
[[nodiscard]] replay_inspection::Key key_of(SDL_Scancode scancode) noexcept;
[[nodiscard]] SDL_Scancode scancode_of(replay_inspection::Key key) noexcept;

} // namespace ayther::runtime
