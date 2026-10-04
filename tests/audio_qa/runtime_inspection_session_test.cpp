// Spec 002, the replay inspection in the Runtime loop, with the real Runtime, the synthetic core
// and a synthetic supervisor, without GPU (scripted input, presentation none). One group per
// task:
//   BR-137 (RF-4.1, RF-4.2, RF-4.4, RF-4.6) the loop stops producing in pause; resuming restarts
//          the cadence deadline without catching up, and the pause is user time.
//   BR-138 (RF-4.1, RF-4.2, RF-5.4) the pause drains and loses no PCM; navigation is silent.
//   BR-140 (RF-5.1, RF-5.2, RF-5.3, RF-3.3, RF-3.6) going back restores, re-simulates with the
//          recorded inputs and presents the target; pack, assignments and conditions do not
//          change: the take still ends in the linear final state.
//   BR-141 (RF-2.13, RNF-1) the facts and PCM of the silent re-simulation never reach the
//          evidence: no fact is duplicated.
//   BR-142 (RF-5.1, RF-5.4, RF-5.7) → in pause produces k+1 silently; Space from j executes j+1
//          with audio without repeating j.
//   BR-143 (RF-5.6) with damaged checkpoints the window stays at the confirmed position with
//          its notice and can resume; if that is not possible either, the traversal fails.
//   BR-146 (RF-7.5, RF-7.6) every produced or recovered frame is a new record with its visit;
//          a pause does not change it.
//   BR-147 (RF-7.4) the processing time is measured; without presentation there is no FPS.
//   BR-151 (RNF-6) without inspection the take replays as before.
// Arguments: runtime, core, ROM, fixture generator, work directory.
#include "runtime_session_harness.h"

#include <algorithm>
#include <cmath>
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

constexpr std::uint32_t frames = 120U;
constexpr double period_ms = 1000.0 / 60.0;

hn::Options base;

hn::Session run(const std::string &label, const std::string &script) {
    auto options = base;
    options.label = label;
    options.script = script;
    return hn::run(options);
}

const qa::ReplayExecutionResult *terminal(const hn::Session &session) {
    return session.terminals.empty() ? nullptr : &session.terminals.front();
}

std::string digest(const qa::ContentIdentity &identity) {
    std::string text;
    for (const auto byte : identity.sha256)
        text += std::to_string(byte) + ".";
    return text;
}

bool has_event(const hn::Session &session, std::string_view control, std::uint64_t before,
               std::uint64_t after) {
    return std::any_of(session.events.begin(), session.events.end(),
                       [&](const qa::InspectionEvent &event) {
                           return event.control == control && event.frame_before == before &&
                                  event.frame_after == after;
                       });
}

std::size_t records_of(const hn::Session &session, std::uint64_t frame) {
    return static_cast<std::size_t>(std::count_if(
        session.render_frames.begin(), session.render_frames.end(),
        [frame](const qa::RenderFrameFact &record) { return record.frame == frame; }));
}

std::uint64_t frames_of_pcm(std::uint64_t bytes, std::uint64_t per_frame) {
    return per_frame == 0U ? 0U : (bytes + per_frame / 2U) / per_frame;
}

// The capture follows the audio device, so a take's PCM is only close to N frames (a few frames
// of jitter between runs). The bounds below still tell apart any leak of silent production.
bool within(std::uint64_t measured, std::uint64_t low, std::uint64_t high) {
    return measured >= low && measured <= high;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 6) {
        std::cerr << "usage: runtime_inspection_session_test <runtime> <core> <rom> <fixture> "
                     "<work>\n";
        return 2;
    }
    const fs::path work{argv[5]};
    fs::create_directories(work);
    const auto take = work / "take-120.arp";
    const auto command = "\"\"" + std::string{argv[4]} + "\" \"" + take.string() + "\" --frames " +
                         std::to_string(frames) + "\"";
    if (std::system(command.c_str()) != 0) {
        std::cerr << "the fixture of 120 frames could not be written\n";
        return 2;
    }
    base.runtime = argv[1];
    base.core = argv[2];
    base.rom = argv[3];
    base.take = take;
    base.root = work;

    // BR-151 (RNF-6): without inspection, the take replays linearly and records every frame once.
    const auto linear = run("linear", {});
    const auto *linear_terminal = terminal(linear);
    expect(linear.stream_valid && linear_terminal && linear_terminal->playback == "natural_end" &&
               linear_terminal->traversal == "linear" && linear_terminal->linear_completed &&
               linear.events.empty(),
           "RNF-6: without inspection the take ends naturally and linearly, without events");
    const auto pcm_per_frame = linear.pcm_bytes / frames;
    expect(pcm_per_frame > 0U, "the linear take captured PCM");
    const auto linear_final = linear_terminal ? digest(linear_terminal->final_game_state) : "";

    // BR-146, BR-147: one record per frame, first visit, processing time and no FPS.
    bool records = linear.render_frames.size() == frames;
    for (const auto &record : linear.render_frames)
        records = records && record.visit == 1U && record.processing_ms && !record.fps_instant;
    expect(records, "RF-7.5, RF-7.4: each frame has one record with its processing time and no "
                    "FPS without presentation");

    // BR-137: pause after frame 20 (it lands on 21), 400 ms of user pause, then resume.
    const auto paused = run("pause", "frame=20 key space down\nafter=0 key space up\n"
                                     "paused=21 key other up\nafter=400 key space down\n"
                                     "after=0 key space up\n");
    const auto *paused_terminal = terminal(paused);
    const auto paused_state =
        std::find_if(paused.states.begin(), paused.states.end(),
                     [](const hn::TimedState &state) { return state.state.phase == "paused"; });
    const auto resumed_state =
        std::find_if(paused_state, paused.states.end(),
                     [](const hn::TimedState &state) { return state.state.phase == "playing"; });
    expect(paused_state != paused.states.end() && paused_state->state.frame == 21U,
           "RF-4.1: the pause lands at the end of the frame in progress");
    expect(paused_terminal && paused_terminal->playback == "natural_end" &&
               paused_terminal->traversal == "linear" && paused_terminal->linear_completed &&
               paused_terminal->user_pause_ms >= 400U,
           "RF-4.6: a pause keeps the traversal linear and is user time");
    expect(resumed_state != paused.states.end() &&
               paused.terminal_at_ms - resumed_state->at_ms >= (frames - 22U) * period_ms * 0.9,
           "RF-4.4: after resuming the take does not catch up the pause");
    expect(records_of(paused, 21U) == 1U && paused.render_frames.size() == frames,
           "RF-7.6: a pause adds no record");
    expect(has_event(paused, "pause", 21U, 21U) && has_event(paused, "resume", 21U, 21U),
           "C2: the pause and the resume are inspection events");
    // BR-138: the pause drains; it neither loses nor adds PCM.
    expect(within(frames_of_pcm(paused.pcm_bytes, pcm_per_frame), frames - 5U, frames + 5U),
           "RF-4.1, RF-4.2: the pause loses no audio and adds none");
    expect(paused_terminal && digest(paused_terminal->final_game_state) == linear_final,
           "a pause does not change the replay");

    // BR-140, BR-138, BR-141: pause at 31, three steps back to 28, resume.
    const auto back = run("back", "frame=30 key space down\nafter=0 key space up\n"
                                  "paused=31 key left down\nafter=0 key left up\n"
                                  "paused=30 key left down\nafter=0 key left up\n"
                                  "paused=29 key left down\nafter=0 key left up\n"
                                  "paused=28 key space down\nafter=0 key space up\n");
    const auto *back_terminal = terminal(back);
    expect(back.stream_valid && back_terminal && back_terminal->playback == "natural_end" &&
               back_terminal->traversal == "inspection" && !back_terminal->linear_completed,
           "RF-5.8: navigating makes the traversal an inspection");
    expect(back_terminal && digest(back_terminal->final_game_state) == linear_final &&
               back_terminal->assignment_count == linear_terminal->assignment_count &&
               back_terminal->inputs_consumed == frames,
           "RF-5.2, RF-3.3: re-simulating with the recorded inputs keeps the replay; the "
           "assignments do not change");
    expect(has_event(back, "step_back", 31U, 30U) && has_event(back, "step_back", 30U, 29U) &&
               has_event(back, "step_back", 29U, 28U) && has_event(back, "resume", 28U, 28U),
           "RF-5.1: every step is an inspection event");
    const auto back_states =
        std::count_if(back.states.begin(), back.states.end(), [](const hn::TimedState &state) {
            return state.state.phase == "paused" && state.state.frame == 28U;
        });
    expect(back_states >= 1, "RF-5.1: the window presents the target, paused");
    // The three frames 29, 30, 31 play again with audio after the resume; nothing else sounds.
    // A leak of the silent re-simulation of frames 0..28 would add some 28 frames.
    expect(within(frames_of_pcm(back.pcm_bytes, pcm_per_frame), frames - 5U, frames + 10U),
           "RF-5.4: navigation is silent; only the frames played again after the resume sound");
    expect(back.duplicate_fact_ids == 0U, "RF-2.13: no fact is duplicated");
    bool silent_excluded = true;
    for (std::uint64_t frame = 0; frame < 28U; ++frame) {
        const auto key = std::make_pair(std::string{"run-back"}, frame);
        const auto linear_key = std::make_pair(std::string{"run-linear"}, frame);
        const auto found = back.engine_facts_by_frame.find(key);
        const auto expected = linear.engine_facts_by_frame.find(linear_key);
        const auto count = found == back.engine_facts_by_frame.end() ? 0U : found->second;
        const auto reference =
            expected == linear.engine_facts_by_frame.end() ? 0U : expected->second;
        silent_excluded = silent_excluded && count == reference;
    }
    expect(silent_excluded, "RF-2.13, RNF-1: the silent re-simulation adds no engine fact");
    expect(records_of(back, 30U) == 3U && records_of(back, 28U) == 2U,
           "RF-7.5: every visit to a frame is a new record");

    // BR-140: going back across the checkpoint of frame 59 restores it mid-take.
    const auto across = run("across", "frame=90 key space down\nafter=0 key space up\n"
                                      "paused=91 key left down\nafter=0 key left up\n"
                                      "paused=90 key left down\nafter=0 key left up\n"
                                      "paused=89 key space down\nafter=0 key space up\n");
    const auto *across_terminal = terminal(across);
    expect(across_terminal && digest(across_terminal->final_game_state) == linear_final &&
               has_event(across, "step_back", 91U, 90U) &&
               has_event(across, "step_back", 90U, 89U) &&
               !has_event(across, "recover_failed", 90U, 91U),
           "RF-5.2: a checkpoint taken during the take restores and re-simulates to the target");

    // BR-142: → ten times in pause produces 12..21 silently; Space from 21 plays 22 with audio.
    std::string steps = "frame=10 key space down\nafter=0 key space up\n";
    for (std::uint32_t frame = 11U; frame <= 20U; ++frame)
        steps += "paused=" + std::to_string(frame) + " key right down\nafter=0 key right up\n";
    steps += "paused=21 key space down\nafter=0 key space up\n";
    const auto forward = run("forward", steps);
    const auto *forward_terminal = terminal(forward);
    expect(forward_terminal && digest(forward_terminal->final_game_state) == linear_final &&
               forward_terminal->traversal == "inspection" &&
               has_event(forward, "step_forward", 11U, 12U) &&
               has_event(forward, "step_forward", 20U, 21U) && records_of(forward, 12U) == 1U &&
               records_of(forward, 22U) == 1U,
           "RF-5.1: → in pause produces k+1 and presents it");
    // Ten silent frames are ten frames less than the linear take; a leak would keep N.
    expect(within(frames_of_pcm(forward.pcm_bytes, pcm_per_frame), frames - 16U, frames - 5U),
           "RF-5.4, RF-5.7: frames 12 to 21 are silent and 22 follows without repeating 21");

    // BR-143: damaged checkpoints; the window stays at 71, resumes and ends naturally.
    const auto damaged = run("damaged", "frame=70 key space down\nafter=0 key space up\n"
                                        "paused=71 corrupt_checkpoints\n"
                                        "after=0 key left down\nafter=0 key left up\n"
                                        "after=300 key space down\nafter=0 key space up\n");
    const auto *damaged_terminal = terminal(damaged);
    expect(has_event(damaged, "recover_failed", 70U, 71U) && damaged_terminal &&
               damaged_terminal->playback == "natural_end" &&
               digest(damaged_terminal->final_game_state) == linear_final,
           "RF-5.6: a failed recovery stays at the confirmed position and can resume");
    const auto lost = run("lost", "frame=70 key space down\nafter=0 key space up\n"
                                  "paused=71 corrupt_visual_state\n"
                                  "after=0 key left down\nafter=0 key left up\n");
    const auto *lost_terminal = terminal(lost);
    expect(lost_terminal && lost_terminal->playback == "failed" &&
               lost_terminal->traversal == "inspection" && !lost_terminal->succeeded,
           "RF-5.6: when the confirmed position cannot be restored either, the traversal fails");

    std::cout << "pcm_per_frame=" << pcm_per_frame
              << " linear_records=" << linear.render_frames.size()
              << " pause_pcm_frames=" << frames_of_pcm(paused.pcm_bytes, pcm_per_frame)
              << " back_pcm_frames=" << frames_of_pcm(back.pcm_bytes, pcm_per_frame)
              << " forward_pcm_frames=" << frames_of_pcm(forward.pcm_bytes, pcm_per_frame) << '\n';
    if (failures != 0)
        return 1;
    std::cout << "the Runtime loop inspects the take\n";
    return 0;
}
