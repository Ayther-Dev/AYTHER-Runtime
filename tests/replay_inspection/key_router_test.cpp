// Spec 002 (plan §5.5): the key router of the replay window.
//   BR-120 (RF-4.5, RF-6.4, RF-3.2)  one action per physical press, repeats ignored, only
//                                    Space, arrows and I considered.
//   BR-121 (RF-4.7, RF-4.9, RF-6.5)  no action without focus; a key held across the focus
//                                    change waits for its release; a captured key does not act.
//   BR-122 (RF-5.10, RF-5.9)         ← and → together do not move until a single new press;
//                                    ↑ and ↓ never move.
#include "key_router.h"

#include <iostream>
#include <optional>
#include <string_view>

namespace ri = ayther::replay_inspection;
using Key = ri::Key;
using Action = ri::KeyAction;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

void edge_and_repeat() {
    ri::KeyRouter router;
    expect(router.key_down(Key::space, false) == Action::toggle,
           "RF-4.5: a press of Space toggles once");
    expect(!router.key_down(Key::space, true) && !router.key_down(Key::space, true),
           "RF-4.5: a held key's repeats do nothing");
    router.key_up(Key::space);
    expect(router.key_down(Key::space, false) == Action::toggle,
           "RF-4.5: a new physical press acts again");
    expect(router.key_down(Key::info, false) == Action::info && !router.key_down(Key::info, true),
           "RF-6.4: I toggles once per press");
    expect(router.key_down(Key::left, false) == Action::left, "RF-5.1: ← moves back");
    router.key_up(Key::left);
    expect(router.key_down(Key::right, false) == Action::right, "RF-5.1: → moves forward");
    router.key_up(Key::right);
    expect(!router.key_down(Key::other, false) && !router.key_down(Key::other, true),
           "RF-3.2: any other key is discarded and never reaches the game");
}

void focus_and_capture() {
    ri::KeyRouter router;
    router.focus(false);
    expect(!router.key_down(Key::space, false), "RF-4.7: without focus there is no action");
    router.focus(true);
    expect(!router.key_down(Key::space, false),
           "RF-4.9: a key held across the focus change does not act");
    router.key_up(Key::space);
    expect(router.key_down(Key::space, false) == Action::toggle,
           "RF-4.9: after its release the key acts again");
    router.key_up(Key::space);
    expect(!router.key_down(Key::right, false, true),
           "RF-6.5: a key captured by the focused control does not act");
    router.key_up(Key::right);
    expect(router.key_down(Key::right, false) == Action::right,
           "RF-6.5: the next uncaptured press acts");
}

void simultaneous_and_vertical() {
    ri::KeyRouter router;
    expect(router.key_down(Key::left, false) == Action::left, "a single ← moves");
    expect(!router.key_down(Key::right, false), "RF-5.10: → while ← is held does not move");
    router.key_up(Key::right);
    expect(!router.key_down(Key::right, false), "RF-5.10: no arrow moves until both are released");
    router.key_up(Key::right);
    router.key_up(Key::left);
    expect(router.key_down(Key::right, false) == Action::right,
           "RF-5.10: after both are released a single new press moves");
    router.key_up(Key::right);
    expect(!router.key_down(Key::up, false) && !router.key_down(Key::down, false),
           "RF-5.9: ↑ and ↓ never move");
}

} // namespace

int main() {
    edge_and_repeat();
    focus_and_capture();
    simultaneous_and_vertical();
    if (failures != 0)
        return 1;
    std::cout << "keys are routed by edge, focus and arrow rules\n";
    return 0;
}
