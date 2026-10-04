// Spec 002, BR-135 and BR-153 (RF-4, RF-5, RF-6, RF-2.6): the scripted input of the QA tests.
// A script drives the inspection without a person: keys, focus, faults and closing, each one
// when the take reaches a frame, when it is paused at a frame, or some time after the step
// before. It is parsed and evaluated without SDL, clock or disk.
#include "input_script.h"

#include <iostream>
#include <string_view>

namespace ri = ayther::replay_inspection;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

void parsing() {
    const auto parsed = ri::parse_input_script("# pause at 3, step back, resume\n"
                                               "frame=3 key space down\n"
                                               "after=0 key space up\n"
                                               "paused=4 key left down\n"
                                               "after=20 key left up\n"
                                               "after=0 key right repeat\n"
                                               "after=0 focus off\n"
                                               "after=0 focus on\n"
                                               "after=0 video_fail 2\n"
                                               "after=0 audio_removed\n"
                                               "after=0 audio_added\n"
                                               "after=0 minimize\n"
                                               "after=0 restore\n"
                                               "\n"
                                               "paused=3 close\n"
                                               "after=0 corrupt_checkpoints\n"
                                               "after=0 scroll page_down\n");
    const auto *steps = std::get_if<std::vector<ri::ScriptStep>>(&parsed);
    expect(steps != nullptr && steps->size() == 15U, "every step is read, comments skipped");
    if (steps == nullptr || steps->size() != 15U)
        return;
    expect((*steps)[0].trigger.kind == ri::ScriptTriggerKind::frame &&
               (*steps)[0].trigger.value == 3U &&
               (*steps)[0].action.kind == ri::ScriptActionKind::key_down &&
               (*steps)[0].action.key == ri::Key::space,
           "frame=3 key space down");
    expect((*steps)[2].trigger.kind == ri::ScriptTriggerKind::paused &&
               (*steps)[2].action.key == ri::Key::left,
           "paused=4 key left down");
    expect((*steps)[4].action.kind == ri::ScriptActionKind::key_repeat &&
               (*steps)[5].action.kind == ri::ScriptActionKind::focus_lost &&
               (*steps)[6].action.kind == ri::ScriptActionKind::focus_gained,
           "repeat and focus are read");
    expect((*steps)[7].action.kind == ri::ScriptActionKind::video_fail &&
               (*steps)[7].action.count == 2U &&
               (*steps)[8].action.kind == ri::ScriptActionKind::audio_removed &&
               (*steps)[12].action.kind == ri::ScriptActionKind::close &&
               (*steps)[13].action.kind == ri::ScriptActionKind::corrupt_checkpoints &&
               (*steps)[14].action.kind == ri::ScriptActionKind::scroll &&
               (*steps)[14].action.scroll == ri::ScrollKey::page_down,
           "faults and closing are read");
    for (const auto *invalid :
         {"frame=x key space down\n", "frame=1 key enter down\n", "soon key space down\n",
          "frame=1 key space\n", "frame=1 video_fail\n", "frame=1 dance\n"}) {
        const auto rejected = ri::parse_input_script(invalid);
        expect(std::holds_alternative<ri::ScriptError>(rejected) &&
                   std::get<ri::ScriptError>(rejected).line == 1U,
               std::string{"an invalid step is rejected with its line: "} + invalid);
    }
}

void evaluation() {
    auto parsed = ri::parse_input_script("frame=3 key space down\n"
                                         "after=0 key space up\n"
                                         "paused=4 key left down\n"
                                         "after=50 key left up\n");
    ri::InputScript script{std::get<std::vector<ri::ScriptStep>>(parsed)};
    expect(script.due({std::nullopt, false, 0.0}).empty(), "nothing before the first frame");
    expect(script.due({2U, false, 10.0}).empty(), "nothing before frame 3");
    const auto pressed = script.due({3U, false, 20.0});
    expect(pressed.size() == 2U && pressed[0].kind == ri::ScriptActionKind::key_down &&
               pressed[1].kind == ri::ScriptActionKind::key_up,
           "frame=3 fires and the step after=0 follows at once");
    expect(script.due({4U, false, 30.0}).empty(), "paused=4 waits for the pause");
    expect(script.due({4U, true, 40.0}).size() == 1U, "paused=4 fires when paused at 4");
    expect(script.due({4U, true, 80.0}).empty(), "after=50 waits 50 ms after the step before");
    const auto released = script.due({4U, true, 95.0});
    expect(released.size() == 1U && released[0].kind == ri::ScriptActionKind::key_up &&
               script.finished(),
           "after=50 fires and the script ends");
    expect(script.due({5U, true, 500.0}).empty(), "a finished script does nothing");
}

// BR-156 (measurement driver): a key that does not reach the pause must not leave the script
// waiting forever. `paused=<k> retry=<ms>` presses the last key again once when the take is not
// paused at k within ms of the step before; after that retry any pause at k or later lets the
// script go on, and a second miss abandons the script. Retries and abandonment are reported.
void retry() {
    const auto parsed = ri::parse_input_script("frame=3 key space down\n"
                                               "after=0 key space up\n"
                                               "paused=4 retry=100 key other up\n"
                                               "after=10 key space down\n");
    const auto *steps = std::get_if<std::vector<ri::ScriptStep>>(&parsed);
    expect(steps != nullptr && steps->size() == 4U && (*steps)[2].trigger.retry_ms == 100U &&
               (*steps)[2].action.kind == ri::ScriptActionKind::key_up &&
               (*steps)[2].action.key == ri::Key::other && !(*steps)[0].trigger.retry_ms,
           "paused=4 retry=100 is read with its action");
    for (const auto *invalid : {"frame=1 retry=100 key space down\n",
                                "paused=1 retry=x key space down\n", "paused=1 retry=100\n"}) {
        const auto rejected = ri::parse_input_script(invalid);
        expect(std::holds_alternative<ri::ScriptError>(rejected),
               std::string{"a retry only waits on a pause, with a number: "} + invalid);
    }
    if (steps == nullptr)
        return;

    ri::InputScript script{*steps};
    expect(script.due({3U, false, 0.0}).size() == 2U, "the pause key is pressed at frame 3");
    expect(script.due({5U, false, 99.0}).empty(), "within 100 ms the script waits");
    const auto again = script.due({6U, false, 100.0});
    expect(again.size() == 2U && again[0].kind == ri::ScriptActionKind::key_down &&
               again[0].key == ri::Key::space && again[0].retry &&
               again[1].kind == ri::ScriptActionKind::key_up && again[1].key == ri::Key::space &&
               again[1].retry && script.retries() == 1U,
           "after 100 ms the last key is pressed again once, marked as a retry");
    expect(script.due({7U, false, 150.0}).empty(), "the retry is not repeated");
    const auto landed = script.due({7U, true, 160.0});
    expect(landed.size() == 1U && landed[0].kind == ri::ScriptActionKind::key_up &&
               !landed[0].retry && !script.abandoned(),
           "after the retry a pause at a later frame lets the script go on");

    ri::InputScript lost{*steps};
    (void)lost.due({3U, false, 0.0});
    (void)lost.due({6U, false, 100.0});
    expect(lost.due({9U, false, 199.0}).empty() && !lost.abandoned(),
           "the retry has its own 100 ms");
    expect(lost.due({10U, false, 200.0}).empty() && lost.abandoned() && lost.finished(),
           "a second miss abandons the script instead of waiting forever");

    ri::InputScript plain{*steps};
    (void)plain.due({3U, false, 0.0});
    expect(plain.due({4U, true, 30.0}).size() == 1U && plain.retries() == 0U,
           "a pause that lands needs no retry");
}

} // namespace

int main() {
    parsing();
    evaluation();
    retry();
    if (failures != 0)
        return 1;
    std::cout << "the input script drives the inspection in order\n";
    return 0;
}
