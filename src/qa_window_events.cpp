#include "qa_window_events.h"

namespace ayther::runtime {

using replay_inspection::Key;

Key key_of(const SDL_Scancode scancode) noexcept {
    switch (scancode) {
    case SDL_SCANCODE_SPACE:
        return Key::space;
    case SDL_SCANCODE_LEFT:
        return Key::left;
    case SDL_SCANCODE_RIGHT:
        return Key::right;
    case SDL_SCANCODE_UP:
        return Key::up;
    case SDL_SCANCODE_DOWN:
        return Key::down;
    case SDL_SCANCODE_I:
        return Key::info;
    default:
        return Key::other;
    }
}

SDL_Scancode scancode_of(const Key key) noexcept {
    switch (key) {
    case Key::space:
        return SDL_SCANCODE_SPACE;
    case Key::left:
        return SDL_SCANCODE_LEFT;
    case Key::right:
        return SDL_SCANCODE_RIGHT;
    case Key::up:
        return SDL_SCANCODE_UP;
    case Key::down:
        return SDL_SCANCODE_DOWN;
    case Key::info:
        return SDL_SCANCODE_I;
    case Key::other:
        break;
    }
    return SDL_SCANCODE_A;
}

std::optional<PresentationEvent> translate_window_event(const SDL_Event &event,
                                                        const SDL_WindowID own) noexcept {
    using Kind = PresentationEvent::Kind;
    switch (event.type) {
    case SDL_EVENT_QUIT:
        return PresentationEvent{Kind::close};
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (event.window.windowID == own)
            return PresentationEvent{Kind::close};
        return std::nullopt;
    case SDL_EVENT_KEY_DOWN: {
        using replay_inspection::ScrollKey;
        const auto scroll = event.key.scancode == SDL_SCANCODE_PAGEUP     ? ScrollKey::page_up
                            : event.key.scancode == SDL_SCANCODE_PAGEDOWN ? ScrollKey::page_down
                            : event.key.scancode == SDL_SCANCODE_HOME     ? ScrollKey::home
                            : event.key.scancode == SDL_SCANCODE_END ? ScrollKey::end
                                                                     : std::optional<ScrollKey>{};
        if (scroll)
            return PresentationEvent{Kind::scroll, Key::other, event.key.repeat, true, *scroll};
        return PresentationEvent{Kind::key_down, key_of(event.key.scancode), event.key.repeat,
                                 true};
    }
    case SDL_EVENT_MOUSE_WHEEL:
        if (event.wheel.windowID != own || event.wheel.y == 0.0F)
            return std::nullopt;
        return PresentationEvent{Kind::scroll, Key::other, false, true,
                                 event.wheel.y > 0.0F ? replay_inspection::ScrollKey::wheel_up
                                                      : replay_inspection::ScrollKey::wheel_down};
    case SDL_EVENT_KEY_UP:
        return PresentationEvent{Kind::key_up, key_of(event.key.scancode), false, false};
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (event.window.windowID != own)
            return std::nullopt;
        return PresentationEvent{Kind::focus, Key::other, false,
                                 event.type == SDL_EVENT_WINDOW_FOCUS_GAINED};
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
        if (event.window.windowID != own)
            return std::nullopt;
        return PresentationEvent{Kind::minimized, Key::other, false,
                                 event.type == SDL_EVENT_WINDOW_MINIMIZED};
    case SDL_EVENT_AUDIO_DEVICE_REMOVED:
        // Only a playback device can be the one the replay uses.
        if (event.adevice.recording)
            return std::nullopt;
        return PresentationEvent{Kind::audio_removed, Key::other, false, true};
    case SDL_EVENT_AUDIO_DEVICE_ADDED:
        if (event.adevice.recording)
            return std::nullopt;
        return PresentationEvent{Kind::audio_added};
    default:
        // The gamepad and the gameplay shortcuts never reach the game.
        return std::nullopt;
    }
}

} // namespace ayther::runtime
