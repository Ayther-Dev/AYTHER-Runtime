// Spec 002, the visible replay inspection with a window, a GPU and simulated events
// (windows-qa-gpu), with the real Runtime, the synthetic core and a synthetic supervisor:
//   BR-144 (RF-6.1, RF-6.6) the overlay starts hidden in every take and I toggles it.
//   BR-145 (RF-7.1, RNF-4; D15) with the overlay visible the wheel and Page Down scroll it, and
//          Space and the arrows still belong to the inspection.
//   BR-147 (RF-7.4) in continuous playback the instantaneous FPS is measured; the first frame,
//          the first after a resume and a navigation visit have none.
//   BR-148 (RF-2.6, RF-4.11) injected video and audio losses stop the take at the last completed
//          frame; on recovery it stays paused with the incident recorded; cancelling is still
//          possible while interrupted.
//   BR-153 (RF-4.1, RF-4.4, RF-4.5, RF-4.9, RF-5.1, RF-5.5, RF-5.7, RF-5.10, RF-6.1, RF-6.2)
//          pause, resume, navigation, limits, I, focus and held keys.
//   BR-154 (RF-2.6, RF-2.8, RF-4.8) natural end, a take of one frame, post-end inspection and
//          interruptions.
// Arguments: runtime, core, ROM, fixture generator, work directory.
#include "runtime_session_harness.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace qa = ayther::audio_qa;
namespace hn = ayther::audio_qa::harness;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

hn::Options base;

hn::Session run(const std::string &label, const std::string &script, const fs::path &take,
                hn::CancelWhen cancel = hn::CancelWhen::never, bool last_take = true) {
    std::cout << "scenario " << label << std::endl;
    auto options = base;
    options.label = label;
    options.script = script;
    options.take = take;
    options.presentation = "visible";
    options.cancel_when = cancel;
    options.last_take = last_take;
    return hn::run(options);
}

const qa::ReplayExecutionResult *terminal(const hn::Session &session) {
    return session.terminals.empty() ? nullptr : &session.terminals.front();
}

bool has_event(const hn::Session &session, std::string_view control) {
    return std::any_of(
        session.events.begin(), session.events.end(),
        [control](const qa::InspectionEvent &event) { return event.control == control; });
}

bool has_event(const hn::Session &session, std::string_view control, std::uint64_t before,
               std::uint64_t after) {
    return std::any_of(session.events.begin(), session.events.end(),
                       [&](const qa::InspectionEvent &event) {
                           return event.control == control && event.frame_before == before &&
                                  event.frame_after == after;
                       });
}

std::size_t count_event(const hn::Session &session, std::string_view control) {
    return static_cast<std::size_t>(std::count_if(
        session.events.begin(), session.events.end(),
        [control](const qa::InspectionEvent &event) { return event.control == control; }));
}

bool state_seen(const hn::Session &session, std::string_view phase,
                std::optional<std::uint64_t> frame = std::nullopt) {
    return std::any_of(
        session.states.begin(), session.states.end(), [&](const hn::TimedState &state) {
            return state.state.phase == phase && (!frame || state.state.frame == frame);
        });
}

void dump(const hn::Session &session) {
    std::cout << "  states:";
    for (const auto &state : session.states)
        std::cout << ' ' << state.state.phase << '@'
                  << (state.state.frame ? std::to_string(*state.state.frame) : "-")
                  << (state.state.interruption_cause.empty()
                          ? ""
                          : "(" + state.state.interruption_cause + ")");
    std::cout << "\n  events:";
    for (const auto &event : session.events)
        std::cout << ' ' << event.control << ':' << event.frame_before << "->" << event.frame_after;
    if (!session.terminals.empty())
        std::cout << "\n  terminal: " << session.terminals.front().playback.value_or("?") << ' '
                  << session.terminals.front().code
                  << " interruptions=" << session.terminals.front().interruptions;
    std::cout << "\n  valid=" << session.stream_valid << " exit=" << session.exit_code << std::endl;
}

fs::path fixture(const fs::path &work, const char *generator, std::uint32_t frames) {
    const auto take = work / ("take-" + std::to_string(frames) + ".arp");
    const auto command = "\"\"" + std::string{generator} + "\" \"" + take.string() +
                         "\" --frames " + std::to_string(frames) + "\"";
    if (std::system(command.c_str()) != 0)
        std::cerr << "the fixture of " << frames << " frames could not be written\n";
    return take;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 6) {
        std::cerr << "usage: runtime_visible_inspection_test <runtime> <core> <rom> <fixture> "
                     "<work>\n";
        return 2;
    }
    const fs::path work{argv[5]};
    fs::create_directories(work);
    const auto take = fixture(work, argv[4], 120U);
    const auto single = fixture(work, argv[4], 1U);
    base.runtime = argv[1];
    base.core = argv[2];
    base.rom = argv[3];
    base.root = work;

    // BR-144, BR-145, BR-153: pause, I, scroll, Space and the arrows with the overlay visible.
    const auto controls =
        run("controls",
            "frame=11 key space down\nafter=0 key space up\n" // pause after 11 → stays on 11
            "paused=11 key space repeat\n"                    // a held key repeats nothing
            "after=0 key space up\n"
            "after=0 key i down\nafter=0 key i up\n" // overlay visible
            "after=100 scroll page_down\nafter=0 scroll wheel_down\n"
            "after=100 key left down\nafter=0 key left up\n" // ← still navigates: 10
            "paused=10 key right down\n"                     // → moves to 11
            "after=0 key left down\nafter=0 key left up\n"   // ← while → is held: nothing
            "after=0 key left down\nafter=0 key right up\n"  // still locked until both rise
            "after=0 key left up\n"
            "after=100 key up down\nafter=0 key up up\n"  // ↑ never moves
            "after=0 focus off\nafter=0 key space down\n" // without focus Space does nothing
            "after=0 key space up\nafter=0 focus on\n"
            "after=100 key i down\nafter=0 key i up\n"          // overlay hidden again
            "after=100 key space down\nafter=0 key space up\n"  // resume from 11 → 12 with audio
            "paused=119 key right down\nafter=0 key right up\n" // no frame after N−1
            "after=300 close\n",
            take);
    const auto *controls_terminal = terminal(controls);
    expect(controls.stream_valid && controls_terminal != nullptr, "the visible take ends");
    expect(!controls.states.empty() && !controls.states.front().state.overlay_visible,
           "RF-6.6: the overlay starts hidden");
    expect(count_event(controls, "overlay_toggle") == 2U && state_seen(controls, "paused", 11U) &&
               std::any_of(controls.states.begin(), controls.states.end(),
                           [](const hn::TimedState &state) { return state.state.overlay_visible; }),
           "RF-6.2: I toggles the overlay without moving the position");
    expect(count_event(controls, "pause") == 1U,
           "RF-4.5, RF-4.9: a held Space and a Space without focus do nothing");
    expect(has_event(controls, "step_back", 11U, 10U) && count_event(controls, "step_back") == 1U &&
               has_event(controls, "step_forward", 10U, 11U) &&
               count_event(controls, "step_forward") == 1U,
           "D15, RF-5.10: with the overlay visible the arrows still navigate; ← and → together "
           "and ↑ do nothing");
    expect(has_event(controls, "resume", 11U, 11U),
           "RF-5.7: Space resumes from the inspected position");
    expect(controls_terminal && controls_terminal->traversal == "inspection" &&
               controls_terminal->playback == "natural_end" && controls_terminal->ended_paused,
           "RF-4.8: the inspected take reaches its natural end and stays paused at N−1");
    expect(!has_event(controls, "step_forward", 119U, 120U), "RF-5.5: there is no frame after N−1");

    // BR-147: FPS in continuous playback, never for a navigation visit or after a resume.
    std::size_t with_fps{};
    bool navigation_without_fps = true;
    for (const auto &record : controls.render_frames) {
        if (record.fps_instant)
            ++with_fps;
        if ((record.frame == 10U && record.visit == 2U) ||
            (record.frame == 11U && record.visit == 2U))
            navigation_without_fps = navigation_without_fps && !record.fps_instant;
        if (record.frame == 0U)
            navigation_without_fps = navigation_without_fps && !record.fps_instant;
    }
    const auto resumed =
        std::find_if(controls.render_frames.begin(), controls.render_frames.end(),
                     [](const qa::RenderFrameFact &record) { return record.frame == 12U; });
    std::cout << "fps: with=" << with_fps << " nav_ok=" << navigation_without_fps
              << " resumed=" << (resumed != controls.render_frames.end()) << " resumed_fps="
              << (resumed != controls.render_frames.end() && resumed->fps_instant) << std::endl;
    expect(with_fps > 100U && navigation_without_fps && resumed != controls.render_frames.end() &&
               !resumed->fps_instant && resumed->processing_ms,
           "RF-7.4: FPS only between consecutive presentations of continuous playback");

    // BR-148: a video loss and an audio loss, each recovered to a pause; the incidents count.
    const auto losses = run("losses",
                            "frame=20 video_fail 3\n"
                            "after=500 key space down\nafter=0 key space up\n"
                            "frame=40 audio_removed\nafter=300 audio_added\n"
                            "after=500 key space down\nafter=0 key space up\n"
                            "paused=119 close\n",
                            take);
    const auto *losses_terminal = terminal(losses);
    dump(losses);
    expect(state_seen(losses, "interrupted"), "RF-2.6: a loss interrupts the take");
    const auto interrupted =
        std::find_if(losses.states.begin(), losses.states.end(), [](const hn::TimedState &state) {
            return state.state.phase == "interrupted";
        });
    expect(interrupted != losses.states.end() && interrupted->state.frame &&
               *interrupted->state.frame <= 21U &&
               interrupted->state.interruption_cause == "video_acquire_failed",
           "RF-4.11: the take stops at the last completed frame, with its cause");
    expect(has_event(losses, "interrupted") && has_event(losses, "recovered") &&
               state_seen(losses, "paused",
                          interrupted != losses.states.end() && interrupted->state.frame
                              ? interrupted->state.frame
                              : std::nullopt),
           "RF-2.6: on recovery the take stays paused at that frame");
    expect(losses_terminal && losses_terminal->interruptions == 2U &&
               losses_terminal->playback == "natural_end",
           "RF-2.6: both incidents are recorded and the take still ends naturally");
    const auto cancelled =
        run("cancel-interrupted", "frame=20 video_fail 1000\n", take, hn::CancelWhen::interrupted);
    const auto *cancelled_terminal = terminal(cancelled);
    expect(cancelled_terminal && cancelled_terminal->playback == "interrupted" &&
               cancelled.cancel_to_terminal_ms < 2000.0,
           "RF-4.11, P-6: cancelling an interrupted take works and ends it as interrupted");

    // BR-154: a take of one frame ends paused at its only frame.
    const auto one = run("one-frame", "paused=0 close\n", single);
    const auto *one_terminal = terminal(one);
    expect(one_terminal && one_terminal->playback == "natural_end" && one_terminal->ended_paused &&
               one_terminal->inputs_consumed == 1U && one_terminal->linear_completed &&
               state_seen(one, "ended_paused", 0U),
           "RF-2.8: a take of one frame ends naturally, paused at frame 0");

    // BR-150, BR-154 (RF-2.9): navigating after the natural end opens a post-end inspection on a
    // re-armed session; the confirmed result does not change.
    const auto post_end = run("post-end",
                              "paused=119 key left down\nafter=0 key left up\n"
                              "paused=118 key left down\nafter=0 key left up\n"
                              "paused=117 key space down\nafter=0 key space up\n"
                              "after=300 close\n",
                              take);
    dump(post_end);
    expect(post_end.terminals.size() == 2U && post_end.runs_opened.size() == 1U &&
               post_end.runs_opened[0].run_id == "run-post-end-inspection-1" &&
               post_end.runs_opened[0].take_position == 0U,
           "RF-2.9: the post-end inspection opens its own run and ends with its own terminal");
    if (post_end.terminals.size() == 2U) {
        const auto &linear_result = post_end.terminals[0];
        const auto &inspection = post_end.terminals[1];
        expect(linear_result.run_id == "run-post-end" && linear_result.traversal == "linear" &&
                   linear_result.playback == "natural_end" && linear_result.ended_paused &&
                   linear_result.linear_completed,
               "RF-2.9: the confirmed linear result is sent first and unchanged");
        expect(inspection.run_id == "run-post-end-inspection-1" &&
                   inspection.traversal == "post_end_inspection" && !inspection.linear_completed,
               "RF-2.9: the post-end traversal is post_end_inspection");
    }
    expect(state_seen(post_end, "paused", 118U) && state_seen(post_end, "paused", 117U) &&
               has_event(post_end, "step_back", 119U, 118U) &&
               has_event(post_end, "step_back", 118U, 117U),
           "RF-2.9: the re-armed session presents the frames before N−1");
    expect(std::any_of(post_end.render_frames.begin(), post_end.render_frames.end(),
                       [](const qa::RenderFrameFact &record) { return record.frame == 117U; }),
           "RF-5.8: the frames visited after the end are recorded in the new run");

    std::cout << "controls events=" << controls.events.size()
              << " records=" << controls.render_frames.size() << " with_fps=" << with_fps
              << " losses_interruptions=" << (losses_terminal ? losses_terminal->interruptions : 0U)
              << " cancel_interrupted_ms=" << cancelled.cancel_to_terminal_ms << '\n';
    if (failures != 0)
        return 1;
    std::cout << "the visible replay is inspected with simulated events\n";
    return 0;
}
