// Spec 006 (RF-3, RF-4; contracts C1): a synthetic supervisor drives the real Runtime with a
// pad timeline instead of a take, with the synthetic core and no GPU.
//   - From power-on the timeline compiles with the timing of the core, every compiled frame is
//     consumed, the live states carry the compiled N, and a `timeline_compiled` fact records
//     fps, frames, start and the absence of a register shadow.
//   - From a state (the raw serialized state of the core, the shape of a Lab bookmark) the
//     session restores it first and the fact names the state.
//   - A bookmark with a 512-byte `.fm` sidecar seeds the detector after the HD boundary and the
//     fact says `fm_seeded`; a sidecar of another size ends with `timeline_fm_sidecar_invalid`.
//   - A timeline that does not parse ends with `timeline_syntax_invalid` before any frame.
//   - A take request without `material_kind` still replays the take (RNF-1).
// Arguments: runtime, core, ROM, take, state, work directory.
#include "pad_timeline.h"
#include "runtime_session_harness.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace qa = ayther::audio_qa;
namespace harness = ayther::audio_qa::harness;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

fs::path write_timeline(const fs::path &work, const std::string &name, const std::string &text) {
    fs::create_directories(work);
    const auto path = work / name;
    std::ofstream{path, std::ios::binary} << text;
    return path;
}

std::optional<qa::TimelineCompiledRecord> compiled_fact(const harness::Session &session) {
    for (const auto &batch : session.run_fact_batches)
        for (const auto &fact : batch)
            if (auto record = qa::read_timeline_compiled(fact))
                return record;
    return std::nullopt;
}

bool same_identity(const qa::ContentIdentity &left, const qa::ContentIdentity &right) {
    return left.sha256 == right.sha256 && left.byte_size == right.byte_size;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 7) {
        std::cerr << "usage: runtime_session_timeline_test <runtime> <core> <rom> <take> <state> "
                     "<work>\n";
        return 2;
    }
    harness::Options base;
    base.runtime = argv[1];
    base.core = argv[2];
    base.rom = argv[3];
    const fs::path take{argv[4]};
    const fs::path state{argv[5]};
    base.root = argv[6];
    base.take_position = 0;
    base.last_take = true;

    // RF-3.1, RF-4.1, RF-4.2: from power-on.
    {
        auto options = base;
        options.label = "power-on";
        options.material_kind = "timeline";
        options.take = write_timeline(base.root / "timelines", "boot.timeline",
                                      "start power_on\npress b 1f\nwait 1f\npress a 1f\n"
                                      "wait 2f\npress start 1f\n");
        const auto session = harness::run(options);
        expect(session.stream_valid && session.terminals.size() == 1U,
               "power-on: one terminal over a valid stream");
        if (!session.terminals.empty()) {
            const auto &terminal = session.terminals.front();
            expect(terminal.succeeded && terminal.playback == "natural_end" &&
                       terminal.traversal == "linear",
                   "RF-4: the timeline ends naturally and linearly");
            expect(terminal.recording_frames == 6U && terminal.inputs_consumed == 6U,
                   "RF-4.1: six compiled frames, six consumed");
            expect(terminal.take_id == options.take.generic_string(),
                   "RF-4: the terminal names the timeline as its material");
            expect(!same_identity(terminal.initial_game_state, terminal.final_game_state),
                   "RF-4: the buttons changed the state of the core");
        }
        bool totals = !session.states.empty();
        for (const auto &timed : session.states)
            totals = totals && timed.state.frames_total == 6U;
        expect(totals, "RF-4.1: every live state carries the compiled N");
        const auto record = compiled_fact(session);
        expect(record.has_value(), "RF-4.2: a timeline_compiled fact is in the evidence");
        if (record)
            expect(record->fps > 0.0 && record->frames == 6U && record->steps == 5U &&
                       record->start == "power_on" && record->state_id.empty() &&
                       record->fm_sidecar == "not_applicable" && !record->fm_seeded,
                   "RF-4.2: the fact records fps, frames, steps, power_on and no sidecar");
    }

    // RF-3.2: from a state.
    {
        auto options = base;
        options.label = "from-state";
        options.material_kind = "timeline";
        options.take =
            write_timeline(base.root / "timelines", "state.timeline",
                           "start state " + state.generic_string() + "\npress b 2f\nwait 2f\n");
        const auto session = harness::run(options);
        expect(session.stream_valid && session.terminals.size() == 1U,
               "from-state: one terminal over a valid stream");
        if (!session.terminals.empty()) {
            const auto &terminal = session.terminals.front();
            expect(terminal.succeeded && terminal.playback == "natural_end" &&
                       terminal.recording_frames == 4U && terminal.inputs_consumed == 4U,
                   "RF-3.2: the timeline from a state replays its four frames");
        }
        const auto record = compiled_fact(session);
        expect(
            record && record->start == "state" && record->state_id.starts_with("state-") &&
                record->fm_sidecar == "absent" && !record->fm_seeded,
            "RF-3.4, RF-4.2: the fact names the state and says no register shadow was next to it");
    }

    // RF-3.3: a bookmark with its `.fm` sidecar, as the Lab writes it next to the `.sav`.
    {
        const auto bookmarks = base.root / "bookmarks";
        fs::create_directories(bookmarks);
        const auto seeded_state = bookmarks / "seeded.sav";
        fs::copy_file(state, seeded_state, fs::copy_options::overwrite_existing);
        std::string registers(qa::timeline_fm_sidecar_bytes, '\0');
        for (std::size_t i = 0; i < registers.size(); ++i)
            registers[i] = static_cast<char>(i * 7U);
        std::ofstream{bookmarks / "seeded.fm", std::ios::binary} << registers;

        auto options = base;
        options.label = "fm-seeded";
        options.material_kind = "timeline";
        options.take = write_timeline(base.root / "timelines", "seeded.timeline",
                                      "start state " + seeded_state.generic_string() +
                                          "\npress b 2f\nwait 2f\n");
        const auto session = harness::run(options);
        expect(session.terminals.size() == 1U && session.terminals.front().succeeded &&
                   session.terminals.front().playback == "natural_end",
               "RF-3.3: the timeline with a sidecar replays to its natural end");
        const auto record = compiled_fact(session);
        expect(record && record->start == "state" && record->fm_sidecar == "present" &&
                   record->fm_seeded,
               "RF-3.3, RF-4.2: the sidecar is present and seeded the detector");

        const auto short_state = bookmarks / "short.sav";
        fs::copy_file(state, short_state, fs::copy_options::overwrite_existing);
        std::ofstream{bookmarks / "short.fm", std::ios::binary} << std::string(100, '\1');
        auto invalid = base;
        invalid.label = "fm-invalid";
        invalid.material_kind = "timeline";
        invalid.take =
            write_timeline(base.root / "timelines", "short.timeline",
                           "start state " + short_state.generic_string() + "\nwait 2f\n");
        const auto rejected = harness::run(invalid);
        expect(rejected.terminals.size() == 1U && !rejected.terminals.front().succeeded &&
                   rejected.terminals.front().code == "timeline_fm_sidecar_invalid" &&
                   rejected.terminals.front().inputs_consumed == 0U,
               "RF-3.3: a sidecar that is not 512 bytes fails before any frame");
    }

    // RF-2.3: a timeline that does not parse.
    {
        auto options = base;
        options.label = "syntax";
        options.material_kind = "timeline";
        options.take =
            write_timeline(base.root / "timelines", "bad.timeline", "wait 1f\njump 2f\n");
        const auto session = harness::run(options);
        expect(session.terminals.size() == 1U && !session.terminals.front().succeeded &&
                   session.terminals.front().code == "timeline_syntax_invalid" &&
                   session.terminals.front().inputs_consumed == 0U,
               "RF-2.3: a timeline that does not parse fails before any frame");
    }

    // RNF-1: a take request without material_kind.
    {
        auto options = base;
        options.label = "take";
        options.take = take;
        const auto session = harness::run(options);
        expect(session.terminals.size() == 1U && session.terminals.front().succeeded &&
                   session.terminals.front().playback == "natural_end" &&
                   session.terminals.front().inputs_consumed == 6U,
               "RNF-1: a take without material_kind still replays its six inputs");
        expect(!compiled_fact(session), "RNF-1: a take produces no timeline_compiled fact");
    }

    if (failures != 0)
        return 1;
    std::cout << "the Runtime replays pad timelines from power-on and from a state\n";
    return 0;
}
