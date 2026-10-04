// Spec 002 (plan §5.4, §5.11): the inspection controller, pure. Each group is one task:
//   BR-123 (RF-4.1, RF-4.2, RF-4.4, RF-4.10)  playback and pause
//   BR-124 (RF-5.1, RF-5.5, RF-5.11)          navigation
//   BR-125 (RF-5.3, RF-5.6)                   recovery and failure
//   BR-126 (RF-6.1, RF-6.2, RF-6.3, RF-6.6, RF-4.3) I and debug per take
//   BR-127 (RF-4.11, RF-2.10)                 interruption and cancellation
//   BR-128 (RF-2.8, RF-2.9, RF-4.8)           natural end
#include "inspection_state.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string_view>
#include <vector>

namespace ri = ayther::replay_inspection;
using Phase = ri::InspectionPhase;
using Kind = ri::CommandKind;
using Notice = ri::Notice;
using Key = ri::KeyAction;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

bool has(const std::vector<ri::Command> &commands, Kind kind) {
    return std::any_of(commands.begin(), commands.end(),
                       [kind](const ri::Command &command) { return command.kind == kind; });
}

bool has(const std::vector<ri::Command> &commands, Kind kind, std::uint32_t frame) {
    return std::any_of(commands.begin(), commands.end(), [&](const ri::Command &command) {
        return command.kind == kind && command.frame == frame;
    });
}

bool notice(const std::vector<ri::Command> &commands, Notice expected) {
    return std::any_of(commands.begin(), commands.end(), [expected](const ri::Command &command) {
        return command.kind == Kind::notice && command.notice == expected;
    });
}

// A take of 10 frames already playing, its frames 0..k produced.
ri::InspectionController playing_until(std::uint32_t k, bool last_take = true) {
    ri::InspectionController controller{10, last_take};
    (void)controller.prepared();
    for (std::uint32_t frame = 0; frame <= k; ++frame)
        (void)controller.frame_completed(frame);
    return controller;
}

// Paused at k by Space during the wait after k (RF-4.1: no frame in progress).
ri::InspectionController paused_at(std::uint32_t k) {
    auto controller = playing_until(k);
    (void)controller.key(Key::toggle);
    return controller;
}

void playback_and_pause() {
    ri::InspectionController controller{10, true};
    expect(controller.phase() == Phase::preparing && !controller.may_run_frame(),
           "RF-4.10: the take starts preparing and runs no frame");
    const auto early = controller.key(Key::toggle);
    expect(notice(early, Notice::not_available) && controller.phase() == Phase::preparing,
           "RF-4.10: Space while preparing says «no disponible»");
    (void)controller.prepared();
    expect(controller.phase() == Phase::playing && controller.may_run_frame(),
           "RF-4.10: Space while preparing leaves no pending order");
    (void)controller.frame_completed(0);
    (void)controller.frame_completed(1);
    // Space during the wait after frame 1: no frame is in progress, so the last completed one
    // stays (spec.md, Pausa; RF-4.1). Frame 2 is not started.
    const auto waiting = controller.key(Key::toggle, ri::FrameActivity::between_frames);
    expect(controller.phase() == Phase::paused && controller.position() == 1U &&
               !controller.may_run_frame() && has(waiting, Kind::present_frame, 1) &&
               has(waiting, Kind::pause_audio) && !has(waiting, Kind::present_frame, 2),
           "RF-4.1: Space between frames pauses at the last completed frame, presented once");
    expect(notice(controller.key(Key::toggle) /* resume */, Notice::none) ||
               controller.phase() == Phase::playing,
           "RF-4.4: Space in pause resumes");
    expect(controller.phase() == Phase::playing && controller.next_frame() == 2U,
           "RF-4.4: playback resumes at k+1");

    // Space while frame 2 is being produced: that frame finishes and is presented in pause.
    (void)controller.key(Key::toggle, ri::FrameActivity::producing);
    expect(controller.phase() == Phase::pausing && !controller.may_run_frame(),
           "RF-4.1: Space during a frame starts pausing; no new frame starts after it");
    const auto paused = controller.frame_completed(2);
    expect(controller.phase() == Phase::paused && controller.position() == 2U &&
               has(paused, Kind::present_frame, 2) && has(paused, Kind::pause_audio),
           "RF-4.1, RF-4.2: the pause happens at the end of the frame in progress");

    // Before the first frame there is no completed frame to keep: frame 0 finishes.
    ri::InspectionController first{10, true};
    (void)first.prepared();
    (void)first.key(Key::toggle, ri::FrameActivity::between_frames);
    expect(first.phase() == Phase::pausing && first.next_frame() == 0U,
           "RF-4.1: Space before the first frame pauses at the end of frame 0");
}

// P-2 (RF-4.1): playback waits for the turn of each frame; Space during that wait finishes the
// frame in progress at once instead of waiting for its turn, so the pause is presented after its
// production and one presentation, not after the rest of the wait.
void frame_start() {
    using namespace std::chrono_literals;
    const auto due = std::chrono::steady_clock::time_point{} + 100ms;
    expect(!ri::start_frame_now(Phase::playing, due - 5ms, due),
           "playback waits for the turn of the next frame");
    expect(ri::start_frame_now(Phase::playing, due, due) &&
               ri::start_frame_now(Phase::playing, due + 3ms, due),
           "playback starts the frame on its turn or late");
    expect(ri::start_frame_now(Phase::pausing, due - 15ms, due),
           "P-2: a requested pause finishes the frame in progress without waiting for its turn");
    for (const auto phase : {Phase::preparing, Phase::paused, Phase::recovering, Phase::interrupted,
                             Phase::closing, Phase::failed})
        expect(!ri::start_frame_now(phase, due + 1s, due), "only playing and pausing run frames");
}

void navigation() {
    auto controller = paused_at(4);
    const auto back = controller.key(Key::left);
    expect(controller.phase() == Phase::recovering && has(back, Kind::recover_frame, 3),
           "RF-5.1: ← in pause recovers k−1");
    expect(notice(controller.key(Key::left), Notice::busy) &&
               notice(controller.key(Key::toggle), Notice::busy) && controller.target() == 3U,
           "RF-5.11: presses while recovering say «ocupado» and do not accumulate");
    (void)controller.recovery_finished(ri::RecoveryOutcome::presented);
    expect(controller.phase() == Phase::paused && controller.position() == 3U &&
               controller.traversal_inspected(),
           "RF-5.1: the recovered frame becomes the paused position");
    const auto forward = controller.key(Key::right);
    expect(has(forward, Kind::recover_frame, 4), "RF-5.1: → in pause recovers k+1");
    (void)controller.recovery_finished(ri::RecoveryOutcome::presented);

    auto start = paused_at(1);
    (void)start.key(Key::left);
    (void)start.recovery_finished(ri::RecoveryOutcome::presented);
    expect(start.position() == 0U && !has(start.key(Key::left), Kind::recover_frame) &&
               start.phase() == Phase::paused,
           "RF-5.5: ← at frame 0 stays at 0");
    auto end = playing_until(8, false);
    (void)end.key(Key::toggle, ri::FrameActivity::producing);
    (void)end.frame_completed(9);
    const auto beyond = end.key(Key::right);
    expect(end.position() == 9U && !has(beyond, Kind::recover_frame) &&
               !has(beyond, Kind::advance_take),
           "RF-5.5: → at N−1 never jumps to another take");
}

void recovery_and_failure() {
    auto ok = paused_at(5);
    (void)ok.key(Key::left);
    const auto presented = ok.recovery_finished(ri::RecoveryOutcome::presented);
    expect(has(presented, Kind::present_frame, 4) && ok.position() == 4U,
           "RF-5.3: a correct recovery presents its target");
    auto failed = paused_at(5);
    (void)failed.key(Key::left);
    const auto back = failed.recovery_finished(ri::RecoveryOutcome::failed_restored);
    expect(failed.phase() == Phase::paused && failed.position() == 5U &&
               notice(back, Notice::recovery_failed),
           "RF-5.6: a failed recovery returns to the confirmed position");
    auto lost = paused_at(5);
    (void)lost.key(Key::left);
    const auto end = lost.recovery_finished(ri::RecoveryOutcome::failed_unrecoverable);
    expect(lost.phase() == Phase::failed && has(end, Kind::finish_traversal),
           "RF-5.6: without a confirmed position the traversal ends failed");
}

void info_and_debug() {
    auto controller = playing_until(2);
    expect(!controller.debug_visible(), "RF-6.6: each take starts with debug hidden");
    const auto shown = controller.key(Key::info);
    expect(controller.debug_visible() && has(shown, Kind::toggle_debug) &&
               controller.phase() == Phase::playing,
           "RF-6.1: I toggles while playing without changing the state");
    (void)controller.key(Key::toggle, ri::FrameActivity::producing);
    (void)controller.frame_completed(3);
    (void)controller.key(Key::info);
    expect(!controller.debug_visible() && controller.phase() == Phase::paused &&
               controller.position() == 3U,
           "RF-6.2, RF-6.3: I in pause does not change the state or the position");
    auto ended = playing_until(9);
    (void)ended.key(Key::info);
    expect(ended.debug_visible() && ended.position() == 9U && ended.phase() == Phase::paused,
           "RF-4.3: I after the natural end keeps the final frame");
    ri::InspectionController next{10, true};
    expect(!next.debug_visible(), "RF-6.6: the next take starts hidden again");
}

void interruption_and_cancellation() {
    auto controller = playing_until(4);
    (void)controller.presentation_interrupted("video_acquire_failed");
    expect(controller.phase() == Phase::interrupted && !controller.may_run_frame(),
           "RF-4.11: an interruption stops at the last completed frame");
    const auto space = controller.key(Key::toggle);
    expect(notice(space, Notice::interrupted) && controller.phase() == Phase::interrupted &&
               controller.interruption_cause() == "video_acquire_failed",
           "RF-4.11: Space during an interruption keeps the pause with its cause");
    (void)controller.presentation_recovered();
    expect(controller.phase() == Phase::paused && controller.position() == 4U,
           "RF-2.6: after the recovery the take stays paused");
    for (const auto state : {0, 1, 2, 3, 4}) {
        auto target = state == 0   ? ri::InspectionController{10, true}
                      : state == 1 ? playing_until(3)
                      : state == 2 ? paused_at(5)
                                   : paused_at(6);
        if (state == 3)
            (void)target.key(Key::left);
        if (state == 4)
            (void)target.presentation_interrupted("audio_device_removed");
        (void)target.key(Key::toggle);
        const auto closed = target.cancel();
        expect(target.phase() == Phase::closing && has(closed, Kind::close) &&
                   !target.may_run_frame() && target.key(Key::right).empty(),
               "RF-2.10: the cancellation wins in every state and discards what is pending");
    }
}

void natural_end() {
    // RF-2.8, RF-4.1 (DI-13): after N−1 of an intermediate take the next take starts on the next
    // cadence slot; until then no frame runs and the take still shows N−1.
    auto middle = playing_until(8, false);
    const auto next = middle.frame_completed(9);
    expect(!has(next, Kind::advance_take) && middle.phase() == Phase::playing &&
               middle.take_end_pending() && !middle.may_run_frame() && middle.position() == 9U,
           "RF-2.8: after N−1 the next take waits for the next cadence slot");
    const auto slot = middle.take_slot_reached();
    expect(has(slot, Kind::advance_take) && middle.phase() == Phase::closing &&
               !middle.take_end_pending(),
           "RF-2.8: an intermediate take gives way to the next one on its slot");

    // Space in that wait: no frame is in progress, so N−1 stays and the next take does not start.
    auto held_end = playing_until(9, false);
    const auto stop = held_end.key(Key::toggle);
    expect(held_end.phase() == Phase::paused && held_end.position() == 9U &&
               has(stop, Kind::present_frame, 9) && has(stop, Kind::pause_audio) &&
               !has(stop, Kind::advance_take) && !held_end.take_end_pending() &&
               held_end.take_slot_reached().empty(),
           "RF-2.8, RF-4.1: a pause before the next take starts stays on N−1 of this take");
    expect(has(held_end.key(Key::toggle), Kind::advance_take),
           "RF-4.8: Space at the paused N−1 starts the next take");
    auto last = playing_until(8, true);
    const auto ended = last.frame_completed(9);
    expect(last.phase() == Phase::paused && last.position() == 9U && last.ended_naturally() &&
               has(ended, Kind::confirm_linear_result) && !has(ended, Kind::advance_take),
           "RF-2.8, RF-2.9: the last take confirms its result and stays paused at N−1");
    expect(notice(last.key(Key::toggle), Notice::no_next_frame),
           "RF-4.8: Space at N−1 without more takes says there is no next frame");
    auto pending = playing_until(8, false);
    (void)pending.key(Key::toggle, ri::FrameActivity::producing);
    const auto held = pending.frame_completed(9);
    expect(pending.phase() == Phase::paused && pending.position() == 9U &&
               !has(held, Kind::advance_take),
           "RF-2.8: with a pause pending the next take does not start");
    expect(has(pending.key(Key::toggle), Kind::advance_take),
           "RF-4.8: Space at N−1 starts the next take");
}

} // namespace

int main() {
    playback_and_pause();
    frame_start();
    navigation();
    recovery_and_failure();
    info_and_debug();
    interruption_and_cancellation();
    natural_end();
    if (failures != 0)
        return 1;
    std::cout << "the inspection controller follows plan §5.4 and §5.11\n";
    return 0;
}
