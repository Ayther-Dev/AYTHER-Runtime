// Spec 002, BR-136 (RF-4.5, RF-4.9, RF-3.2; plan §5.5): the replay window translates the SDL
// presses, releases, repetitions and focus changes for the KeyRouter. Synthetic SDL events,
// without GPU and without initializing SDL.
#include "qa_window_events.h"

#include "key_router.h"

#include <SDL3/SDL.h>

#include <iostream>
#include <optional>
#include <string_view>

namespace rt = ayther::runtime;
namespace ri = ayther::replay_inspection;

namespace {

int failures = 0;
constexpr SDL_WindowID own_window = 7;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

SDL_Event key(SDL_EventType type, SDL_Scancode scancode, bool repeat = false) {
    SDL_Event event{};
    event.type = type;
    event.key.windowID = own_window;
    event.key.scancode = scancode;
    event.key.down = type == SDL_EVENT_KEY_DOWN;
    event.key.repeat = repeat;
    return event;
}

SDL_Event window(SDL_EventType type, SDL_WindowID id = own_window) {
    SDL_Event event{};
    event.type = type;
    event.window.windowID = id;
    return event;
}

// Feeds one SDL event through the translation and the router, as the replay loop does.
std::optional<ri::KeyAction> feed(ri::KeyRouter &router, const SDL_Event &event) {
    const auto translated = rt::translate_window_event(event, own_window);
    if (!translated)
        return std::nullopt;
    switch (translated->kind) {
    case rt::PresentationEvent::Kind::key_down:
        return router.key_down(translated->key, translated->repeat);
    case rt::PresentationEvent::Kind::key_up:
        router.key_up(translated->key);
        return std::nullopt;
    case rt::PresentationEvent::Kind::focus:
        router.focus(translated->value);
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

void keys() {
    ri::KeyRouter router;
    expect(feed(router, key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE)) == ri::KeyAction::toggle,
           "RF-4.1: Space pauses");
    expect(!feed(router, key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE, true)),
           "RF-4.5: a held Space repeats nothing");
    (void)feed(router, key(SDL_EVENT_KEY_UP, SDL_SCANCODE_SPACE));
    expect(feed(router, key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_LEFT)) == ri::KeyAction::left &&
               feed(router, key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_I)) == ri::KeyAction::info,
           "RF-5.1, RF-6.2: ← and I are routed");
    (void)feed(router, key(SDL_EVENT_KEY_UP, SDL_SCANCODE_LEFT));
    (void)feed(router, key(SDL_EVENT_KEY_UP, SDL_SCANCODE_I));
    const auto other =
        rt::translate_window_event(key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_Z), own_window);
    expect(other && other->key == ri::Key::other && !router.key_down(other->key, false),
           "RF-3.2: any other key is translated as other and never acts");
    expect(rt::translate_window_event(key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_UP), own_window)->key ==
               ri::Key::up,
           "↑ is known so that it never navigates");
}

void focus() {
    ri::KeyRouter router;
    (void)feed(router, window(SDL_EVENT_WINDOW_FOCUS_LOST));
    expect(!feed(router, key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE)),
           "RF-4.7: without focus Space does nothing");
    (void)feed(router, window(SDL_EVENT_WINDOW_FOCUS_GAINED));
    expect(!feed(router, key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE, true)),
           "RF-4.9: a key held across the focus change stays blocked");
    (void)feed(router, key(SDL_EVENT_KEY_UP, SDL_SCANCODE_SPACE));
    expect(feed(router, key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE)) == ri::KeyAction::toggle,
           "RF-4.9: after its release the key acts again");
    expect(!rt::translate_window_event(window(SDL_EVENT_WINDOW_FOCUS_LOST, 99), own_window),
           "the focus of another window is ignored");
}

void window_events() {
    const auto minimized =
        rt::translate_window_event(window(SDL_EVENT_WINDOW_MINIMIZED), own_window);
    expect(minimized && minimized->kind == rt::PresentationEvent::Kind::minimized &&
               minimized->value,
           "RF-2.6: minimizing is reported");
    SDL_Event removed{};
    removed.type = SDL_EVENT_AUDIO_DEVICE_REMOVED;
    removed.adevice.recording = false;
    const auto audio = rt::translate_window_event(removed, own_window);
    expect(audio && audio->kind == rt::PresentationEvent::Kind::audio_removed,
           "RF-4.11: removing the playback device is reported");
    removed.adevice.recording = true;
    expect(!rt::translate_window_event(removed, own_window),
           "a recording device is not the replay's");
    const auto close =
        rt::translate_window_event(window(SDL_EVENT_WINDOW_CLOSE_REQUESTED), own_window);
    expect(close && close->kind == rt::PresentationEvent::Kind::close,
           "RF-2.10: closing the window is reported");
    // BR-145 (D15): Page Down and the wheel scroll the overlay; they are never navigation.
    const auto page =
        rt::translate_window_event(key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_PAGEDOWN), own_window);
    expect(page && page->kind == rt::PresentationEvent::Kind::scroll &&
               page->scroll == ri::ScrollKey::page_down,
           "D15: Page Down scrolls the overlay");
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.windowID = own_window;
    wheel.wheel.y = -1.0F;
    const auto wheeled = rt::translate_window_event(wheel, own_window);
    expect(wheeled && wheeled->kind == rt::PresentationEvent::Kind::scroll &&
               wheeled->scroll == ri::ScrollKey::wheel_down,
           "D15: the wheel scrolls the overlay");
    SDL_Event pad{};
    pad.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    expect(!rt::translate_window_event(pad, own_window), "RF-3.2: the gamepad never acts");
}

} // namespace

int main() {
    keys();
    focus();
    window_events();
    if (failures != 0)
        return 1;
    std::cout << "the replay window routes keys and focus\n";
    return 0;
}
