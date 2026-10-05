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
//   D-11 (DI-15) the Engine facts a recovery produced silently are declared as exclusions, and
//          the evidence of an inspected take, kept and audited as the supervisor does, is
//          complete (without losses).
//   D-7, D-8 (campaign 2026-10-04) with the visible presentation of SDL's offscreen driver, when
//          the Vulkan driver offers it: a frame presented again after a step back counts once
//          and the take ends with its terminal; the overlay shows the current phase and frame.
// Arguments: runtime, core, ROM, fixture generator, work directory.
#include "incremental_evidence.h"
#include "runtime_session_harness.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

// D-11 (DI-15): the evidence of the take kept and audited as the supervisor does: every fact
// batch and PCM chunk of its run in a new evidence directory, reopened and audited at the
// terminal, per segment as for an inspection. Complete means kept without losses.
struct AuditedEvidence {
    bool preserved{};
    bool loss_free{};
    bool relationships{};
    std::string error;
};

AuditedEvidence audit_evidence(const hn::Session &session, const std::string &label) {
    const auto *result = terminal(session);
    if (result == nullptr)
        return {false, false, false, "no_terminal"};
    const auto root = base.root / (label + "-evidence");
    std::error_code ignored;
    fs::remove_all(root, ignored);
    auto opened = qa::open_incremental_evidence(root, result->run_id);
    auto *writer = std::get_if<qa::IncrementalEvidenceWriter>(&opened);
    if (writer == nullptr)
        return {false, false, false, "evidence_unavailable"};
    for (const auto &batch : session.run_fact_batches)
        if (const auto error = writer->append_facts(batch))
            return {false, false, false, std::string{qa::integrated_evidence_error_code(*error)}};
    for (const auto &chunk : session.run_pcm)
        if (const auto error = writer->append_pcm(chunk))
            return {false, false, false, std::string{qa::integrated_evidence_error_code(*error)}};
    const auto finished = writer->finish(result->trace, result->assignment_count > 0U,
                                         qa::PcmContinuity::per_segment);
    if (const auto *error = std::get_if<qa::IntegratedEvidenceError>(&finished))
        return {false, false, false, std::string{qa::integrated_evidence_error_code(*error)}};
    const auto &summary = std::get<qa::IntegratedEvidenceSummary>(finished);
    return {true, summary.fact_integrity_complete, summary.relationships_reopened, {}};
}

bool declared_silent_recoveries(const hn::Session &session) {
    return !session.fact_exclusions.empty() &&
           std::all_of(session.fact_exclusions.begin(), session.fact_exclusions.end(),
                       [](const qa::FactExclusion &exclusion) {
                           return exclusion.cause == "silent_recovery" &&
                                  exclusion.sequence_from <= exclusion.sequence_to;
                       });
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

// The PCM capture follows the audio device: its priming, its tail and the wall time of the take
// move it by some ±5 frames between runs with the same input, so it is only reported. What
// sounded is checked on the Engine facts of each frame instead: the Engine produces the same
// facts for the same inputs (RNF-1), and silent production sends none (DI-8). The Engine numbers
// the frame of take frame k as k + 1; its frame 0 holds the facts of the preparation.
std::uint64_t facts_of(const hn::Session &session, const std::string &label,
                       std::uint64_t engine_frame) {
    const auto found = session.engine_facts_by_frame.find({"run-" + label, engine_frame});
    return found == session.engine_facts_by_frame.end() ? 0U : found->second;
}

// Every take frame k sounded `times(k)` times the facts it has in the linear take.
template <class Times>
bool sounded(const hn::Session &session, const std::string &label, const hn::Session &linear,
             Times times) {
    bool matched = facts_of(session, label, 0U) == facts_of(linear, "linear", 0U);
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        const auto found = facts_of(session, label, frame + 1U);
        const auto expected = times(frame) * facts_of(linear, "linear", frame + 1U);
        if (found != expected) {
            std::cerr << label << " frame " << frame << ": " << found << " engine facts, "
                      << expected << " expected\n";
            matched = false;
        }
    }
    return matched;
}

// The visible presentation without a desktop window: SDL's offscreen video driver, when the Vulkan
// driver offers it. The Runtime writes its marks (BR-156) to `timing.log` of the scenario.
hn::Session run_offscreen(const std::string &label, const std::string &script) {
    auto options = base;
    options.label = label;
    options.script = script;
    options.presentation = "visible";
    options.last_take = true;
    options.extra_environment.push_back({L"SDL_VIDEO_DRIVER", L"offscreen"});
    options.extra_environment.push_back(
        {L"AYTHER_QA_TIMING_LOG", (base.root / label / "timing.log").wstring()});
    return hn::run(options);
}

// The marks of one kind in the timing log of a scenario, as their comma-separated fields.
std::vector<std::vector<std::string>> marks(const std::string &label, std::string_view kind) {
    std::vector<std::vector<std::string>> found;
    std::ifstream input{base.root / label / "timing.log"};
    std::string line;
    while (std::getline(input, line)) {
        std::vector<std::string> fields;
        std::size_t begin{};
        for (;;) {
            const auto comma = line.find(',', begin);
            fields.push_back(line.substr(begin, comma - begin));
            if (comma == std::string::npos)
                break;
            begin = comma + 1U;
        }
        if (!fields.empty() && fields.front() == kind)
            found.push_back(std::move(fields));
    }
    return found;
}

// What the overlay showed last before the window closed: visible flag, phase and frame.
std::optional<std::vector<std::string>> last_overlay(const std::string &label) {
    const auto debug = marks(label, "debug");
    for (auto entry = debug.rbegin(); entry != debug.rend(); ++entry)
        if (entry->size() == 4U && (*entry)[2] != "closing")
            return *entry;
    return std::nullopt;
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
    // D-9, DI-14: the PCM of a linear take is one segment, contiguous.
    expect(linear.pcm_breaks.empty() && linear.pcm_segment_jumps.empty() &&
               linear.pcm_segments == std::vector<std::uint64_t>{0U},
           "D-9, DI-14: a linear take keeps its PCM as one continuous segment");
    const auto pcm_per_frame = linear.pcm_bytes / frames;
    expect(pcm_per_frame > 0U, "the linear take captured PCM");
    const auto linear_final = linear_terminal ? digest(linear_terminal->final_game_state) : "";

    // BR-146, BR-147: one record per frame, first visit, processing time and no FPS.
    bool records = linear.render_frames.size() == frames;
    for (const auto &record : linear.render_frames)
        records = records && record.visit == 1U && record.processing_ms && !record.fps_instant;
    expect(records, "RF-7.5, RF-7.4: each frame has one record with its processing time and no "
                    "FPS without presentation");

    // BR-137: pause after frame 21 (it stays on 21, RF-4.1), 400 ms of user pause, then resume.
    const auto paused = run("pause", "frame=21 key space down\nafter=0 key space up\n"
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
    // BR-138: the pause drains; it neither loses nor adds audio.
    expect(sounded(paused, "pause", linear, [](std::uint64_t) { return 1U; }),
           "RF-4.1, RF-4.2: the pause loses no audio and adds none");
    expect(paused_terminal && digest(paused_terminal->final_game_state) == linear_final,
           "a pause does not change the replay");
    // D-9, DI-14: the resume opens the next segment; without navigation the line does not jump.
    expect(paused.pcm_breaks.empty() && paused.pcm_segment_jumps.empty() &&
               paused.pcm_segments == std::vector<std::uint64_t>{0U, 1U},
           "D-9, DI-14: a pause splits the PCM in two segments with no jump between them");

    // BR-140, BR-138, BR-141: pause at 31, three steps back to 28, resume.
    const auto back = run("back", "frame=31 key space down\nafter=0 key space up\n"
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
    // A leak of the silent re-simulation of frames 0..28 would sound them twice.
    expect(sounded(back, "back", linear,
                   [](std::uint64_t frame) { return frame >= 29U && frame <= 31U ? 2U : 1U; }),
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
    // D-9, DI-14: going back restores a checkpoint and the PCM line goes back; that happens
    // between the segment before the pause and the one after the resume, never within one.
    expect(back.pcm_breaks.empty() && back.pcm_segments == std::vector<std::uint64_t>{0U, 1U},
           "D-9, DI-14: the PCM before and after the steps back are two contiguous segments, got " +
               std::to_string(back.pcm_breaks.size()) + " breaks within a segment");
    // D-11, DI-15: the facts of the silent re-simulation are declared, and the evidence of the
    // inspected take is complete: no undeclared gap, no unresolved cause.
    expect(declared_silent_recoveries(back),
           "D-11, DI-15: each step back declares the facts it produced silently");
    const auto back_evidence = audit_evidence(back, "back");
    expect(back_evidence.preserved && back_evidence.loss_free,
           "D-11, DI-15: the evidence of an inspection with steps back is complete, got " +
               (back_evidence.preserved ? std::string{"loss_free=false"} : back_evidence.error));

    // BR-140: going back across the checkpoint of frame 59 restores it mid-take.
    const auto across = run("across", "frame=91 key space down\nafter=0 key space up\n"
                                      "paused=91 key left down\nafter=0 key left up\n"
                                      "paused=90 key left down\nafter=0 key left up\n"
                                      "paused=89 key space down\nafter=0 key space up\n");
    const auto *across_terminal = terminal(across);
    expect(across_terminal && digest(across_terminal->final_game_state) == linear_final &&
               has_event(across, "step_back", 91U, 90U) &&
               has_event(across, "step_back", 90U, 89U) &&
               !has_event(across, "recover_failed", 90U, 91U),
           "RF-5.2: a checkpoint taken during the take restores and re-simulates to the target");
    expect(across.pcm_breaks.empty() && across.pcm_segments == std::vector<std::uint64_t>{0U, 1U},
           "D-9, DI-14: across a checkpoint the PCM is two contiguous segments, got " +
               std::to_string(across.pcm_breaks.size()) + " breaks within a segment");
    expect(declared_silent_recoveries(across),
           "D-11, DI-15: going back across a checkpoint declares the facts it produced silently");
    const auto across_evidence = audit_evidence(across, "across");
    expect(
        across_evidence.preserved && across_evidence.loss_free,
        "D-11, DI-15: the evidence of an inspection across a checkpoint is complete, got " +
            (across_evidence.preserved ? std::string{"loss_free=false"} : across_evidence.error));

    // BR-142: → ten times in pause produces 12..21 silently; Space from 21 plays 22 with audio.
    std::string steps = "frame=11 key space down\nafter=0 key space up\n";
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
    // The ten frames produced in pause never sound; every other frame sounds once.
    expect(sounded(forward, "forward", linear,
                   [](std::uint64_t frame) { return frame >= 12U && frame <= 21U ? 0U : 1U; }),
           "RF-5.4, RF-5.7: frames 12 to 21 are silent and 22 follows without repeating 21");
    expect(forward.pcm_breaks.empty() && forward.pcm_segments == std::vector<std::uint64_t>{0U, 1U},
           "D-9, DI-14: the frames produced silently leave two contiguous segments, got " +
               std::to_string(forward.pcm_breaks.size()) + " breaks within a segment");
    const auto forward_evidence = audit_evidence(forward, "forward");
    expect(declared_silent_recoveries(forward) && forward_evidence.preserved &&
               forward_evidence.loss_free,
           "D-11, DI-15: the frames produced silently by → are declared and the evidence is "
           "complete");

    // BR-143: damaged checkpoints; the window stays at 71, resumes and ends naturally.
    const auto damaged = run("damaged", "frame=71 key space down\nafter=0 key space up\n"
                                        "paused=71 corrupt_checkpoints\n"
                                        "after=0 key left down\nafter=0 key left up\n"
                                        "after=300 key space down\nafter=0 key space up\n");
    const auto *damaged_terminal = terminal(damaged);
    expect(has_event(damaged, "recover_failed", 70U, 71U) && damaged_terminal &&
               damaged_terminal->playback == "natural_end" &&
               digest(damaged_terminal->final_game_state) == linear_final,
           "RF-5.6: a failed recovery stays at the confirmed position and can resume");
    const auto lost = run("lost", "frame=71 key space down\nafter=0 key space up\n"
                                  "paused=71 corrupt_visual_state\n"
                                  "after=0 key left down\nafter=0 key left up\n");
    const auto *lost_terminal = terminal(lost);
    expect(lost_terminal && lost_terminal->playback == "failed" &&
               lost_terminal->traversal == "inspection" && !lost_terminal->succeeded,
           "RF-5.6: when the confirmed position cannot be restored either, the traversal fails");

    // D-8 (campaign 2026-10-04, BR-185): a visible take where a frame is presented again after
    // stepping back. Pause at 30, ← to 29, resume (30 is presented a second time), natural end
    // paused at 119 and close. Every presentation of a frame (or every failed one) used to
    // count, so the take reported more presented or affected frames than frames consumed; its
    // terminal was malformed, the Runtime could not send it and ended 65 without it, and the
    // supervisor reported `runtime_evidence_stream_invalid`. Each take frame counts once.
    const auto revisited = run_offscreen(
        "visible-revisit", "frame=30 focus on\nafter=0 key space down\nafter=0 key space up\n"
                           "paused=30 key left down\nafter=0 key left up\n"
                           "paused=29 key space down\nafter=0 key space up\n"
                           "paused=119 key other up\nafter=300 close\n");
    const auto *revisited_terminal = terminal(revisited);
    expect(revisited.stream_valid && revisited_terminal != nullptr,
           "D-8, RF-2.12: a visible take with a frame presented again ends with its terminal");
    if (revisited_terminal != nullptr) {
        const auto &shown = revisited_terminal->presentation;
        expect(revisited_terminal->playback == "natural_end" &&
                   revisited_terminal->inputs_consumed == frames &&
                   shown.presented_frames <= revisited_terminal->inputs_consumed &&
                   shown.affected_frames <= revisited_terminal->inputs_consumed &&
                   (shown.affected_frames == 0U ||
                    shown.first_affected_frame <= shown.last_affected_frame),
               "D-8: each take frame counts once among the presented and the affected frames");
    }

    // D-7 (campaign 2026-10-04, BR-185; RF-7.1, RF-7.6): with the overlay visible, after a pause
    // and after a step the overlay shows the current phase and frame, not the phase of the
    // moment its record was copied (playing, recovering).
    const auto paused_overlay =
        run_offscreen("overlay-pause", "frame=0 focus on\nafter=0 key i down\nafter=0 key i up\n"
                                       "frame=30 key space down\nafter=0 key space up\n"
                                       "paused=30 key other up\nafter=400 close\n");
    const auto stepped_overlay =
        run_offscreen("overlay-step", "frame=0 focus on\nafter=0 key i down\nafter=0 key i up\n"
                                      "frame=30 key space down\nafter=0 key space up\n"
                                      "paused=30 key right down\nafter=0 key right up\n"
                                      "paused=31 key other up\nafter=400 close\n");
    const auto paused_last = last_overlay("overlay-pause");
    const auto stepped_last = last_overlay("overlay-step");
    if (marks("overlay-pause", "debug").empty()) {
        std::cout << "overlay: no offscreen presentation; the overlay is not checked\n";
    } else {
        expect(paused_overlay.stream_valid && paused_last &&
                   *paused_last == std::vector<std::string>{"debug", "1", "paused", "30"},
               "D-7, RF-7.1: after a pause the overlay shows the phase paused at 30, got " +
                   (paused_last ? (*paused_last)[2] + " " + (*paused_last)[3] : "nothing"));
        expect(stepped_overlay.stream_valid && stepped_last &&
                   *stepped_last == std::vector<std::string>{"debug", "1", "paused", "31"},
               "D-7, RF-7.6: after a step the overlay shows the phase paused at 31, got " +
                   (stepped_last ? (*stepped_last)[2] + " " + (*stepped_last)[3] : "nothing"));
    }

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
