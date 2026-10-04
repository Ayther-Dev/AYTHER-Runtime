#include "key_router.h"

namespace ayther::replay_inspection {

std::optional<KeyAction> KeyRouter::key_down(Key key, bool repeat, bool captured) {
    if (key == Key::other)
        return std::nullopt;
    const auto slot = index(key);
    const bool already_pressed = pressed_[slot];
    pressed_[slot] = true;
    if (repeat || already_pressed || !focused_ || blocked_[slot])
        return std::nullopt;
    if (captured) {
        // The press belongs to the control; the key stays unusable until released.
        blocked_[slot] = true;
        return std::nullopt;
    }
    switch (key) {
    case Key::space:
        return KeyAction::toggle;
    case Key::info:
        return KeyAction::info;
    case Key::left:
    case Key::right: {
        const bool other_arrow = pressed_[index(key == Key::left ? Key::right : Key::left)];
        if (other_arrow)
            arrows_locked_ = true;
        if (arrows_locked_)
            return std::nullopt;
        return key == Key::left ? KeyAction::left : KeyAction::right;
    }
    case Key::up:
    case Key::down:
    case Key::other:
        return std::nullopt;
    }
    return std::nullopt;
}

void KeyRouter::key_up(Key key) noexcept {
    if (key == Key::other)
        return;
    const auto slot = index(key);
    pressed_[slot] = false;
    blocked_[slot] = false;
    if (!pressed_[index(Key::left)] && !pressed_[index(Key::right)])
        arrows_locked_ = false;
}

void KeyRouter::focus(bool focused) noexcept {
    if (focused && !focused_)
        blocked_ = pressed_;
    focused_ = focused;
}

} // namespace ayther::replay_inspection
