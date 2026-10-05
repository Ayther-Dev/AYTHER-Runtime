// Spec 002: the run_check library with the real Runtime and without GPU.
//   BR-063 (RF-2.5)  a failure stops the request: [A, B fails, C].
//   BR-067 (RF-2.5)  CancelToken sends `cancel` over the open control channel.
//   BR-068 (RNF-5)   cessation confirmed after exit and evidence close; recovery of a run
//                    left open by an interrupted supervisor, without relaunching it.
//   BR-069 (RF-2.3)  phases reach the observer in order.
//   BR-060 (RF-2.11) a material changed between validation and its take stops it.
#include "check_runner.h"
#include "recording_header.h"
#include "request_ledger.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

class Recorder final : public qa::CheckObserver {
  public:
    std::vector<qa::RequestPhase> phases;
    std::vector<qa::TakeOutcome> outcomes;
    std::vector<std::string> lines;
    std::function<void(const qa::RequestPhase &)> on_phase_hook;
    std::function<void(const qa::TakeOutcome &)> on_outcome_hook;

    void on_phase(qa::RequestPhase phase) override {
        phases.push_back(phase);
        if (on_phase_hook)
            on_phase_hook(phase);
    }
    void on_replay_state(const qa::ReplayStateView &) override {}
    void on_take_outcome(const qa::TakeOutcome &outcome) override {
        outcomes.push_back(outcome);
        if (on_outcome_hook)
            on_outcome_hook(outcome);
    }
    void on_report(std::string_view line) override { lines.emplace_back(line); }

    [[nodiscard]] bool reported(std::string_view text) const {
        for (const auto &line : lines)
            if (line.find(text) != std::string::npos)
                return true;
        return false;
    }
};

struct Paths {
    std::filesystem::path runtime, core, rom, take, long_take, rejected_state, root;
};

qa::ReplayRequest request(const Paths &paths, const std::string &output,
                          std::vector<std::string> takes, const std::string &id) {
    qa::ReplayRequest value;
    value.runtime = paths.runtime.string();
    value.rom = paths.rom.string();
    value.core = paths.core.string();
    value.takes = std::move(takes);
    value.output = (paths.root / output).string();
    value.request_id = id;
    return value;
}

std::vector<char> read_file(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void write_file(const std::filesystem::path &path, const std::vector<char> &bytes) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

const qa::TakeOutcome *ran(const qa::RequestOutcome &outcome, std::size_t position) {
    return position < outcome.per_take.size()
               ? std::get_if<qa::TakeOutcome>(&outcome.per_take[position])
               : nullptr;
}

const qa::NotStartedTake *pending(const qa::RequestOutcome &outcome, std::size_t position) {
    return position < outcome.per_take.size()
               ? std::get_if<qa::NotStartedTake>(&outcome.per_take[position])
               : nullptr;
}

std::optional<qa::Run> ledger_run(const std::filesystem::path &output, std::string_view id) {
    auto opened = qa::open_request_ledger(output);
    const auto *ledger = std::get_if<qa::RequestLedger>(&opened);
    return ledger == nullptr ? std::nullopt : ledger->find_run(id);
}

// BR-069: validating, admitted, preparing(0), running(0), closing, closed.
void phases_in_order(const Paths &paths) {
    Recorder observer;
    qa::CancelToken cancel;
    const auto outcome = qa::run_check(
        request(paths, "phases", {paths.take.string()}, "runner-phases"), observer, cancel);
    using K = qa::RequestPhaseKind;
    const std::vector<qa::RequestPhase> expected{{K::validating, 0}, {K::admitted, 0},
                                                 {K::preparing, 0},  {K::running, 0},
                                                 {K::closing, 0},    {K::closed, 0}};
    expect(outcome.exit_code == 0 && observer.phases == expected,
           "RF-2.3: the observer receives the phases in order");
    expect(observer.outcomes.size() == 1U && outcome.confirmed && outcome.linear_complete,
           "RF-2.3: one confirmed take outcome");
    // BR-068: the process exited and the evidence was closed.
    const auto run = ledger_run(paths.root / "phases", "runner-phases");
    expect(run && run->phase == qa::Phase::closed && run->cessation_confirmed,
           "RNF-5: cessation is confirmed after exit and evidence close");
}

// BR-063: [A, B fails, C] keeps A, closes B with its diagnostic and leaves C unstarted. B's
// initial state decompresses whole but the core rejects it, which only the Runtime can find.
void failure_stops_the_request(const Paths &paths) {
    Recorder observer;
    qa::CancelToken cancel;
    const auto outcome = qa::run_check(
        request(paths, "failure",
                {paths.take.string(), paths.rejected_state.string(), paths.take.string()},
                "runner-failure"),
        observer, cancel);
    const auto *first = ran(outcome, 0);
    const auto *second = ran(outcome, 1);
    const auto *third = pending(outcome, 2);
    expect(first != nullptr && first->playback.kind == qa::PlaybackKind::natural_end &&
               first->evidence.complete,
           "RF-2.5: the take before the failure keeps its result");
    expect(second != nullptr && second->playback.kind == qa::PlaybackKind::failed &&
               !second->playback.diagnostic.empty(),
           "RF-2.5: the failed take is closed with its diagnostic");
    expect(third != nullptr && third->reason == "not_started_after_failure",
           "RF-2.5: the take after the failure is not started");
    expect(outcome.exit_code == 2 && !outcome.linear_complete,
           "RF-2.12: the joint result does not credit a linear reproduction");
}

// D-2 (campaign 2026-10-04; RF-2.2): a take whose compressed initial state is damaged is found
// before admission, in its field, and nothing runs.
void damaged_state_is_rejected_before_admission(const Paths &paths) {
    auto bytes = read_file(paths.take);
    const auto layout = qa::decode_recording_layout(std::as_bytes(std::span{bytes}));
    expect(layout.error == qa::RecordingLayoutError::none, "the public take is readable");
    const auto state = layout.layout.compressed_state;
    for (std::uint64_t offset = 0; offset < state.size; ++offset)
        bytes[static_cast<std::size_t>(state.offset + offset)] ^= static_cast<char>(0x5a);
    const auto broken = paths.root / "broken.arp";
    write_file(broken, bytes);
    Recorder observer;
    qa::CancelToken cancel;
    const auto outcome = qa::run_check(
        request(paths, "damaged", {paths.take.string(), broken.string()}, "runner-damaged"),
        observer, cancel);
    expect(outcome.exit_code == 3 &&
               std::find(outcome.issues.begin(), outcome.issues.end(),
                         qa::FieldIssue{"--take[1]", "take_initial_state_invalid"}) !=
                   outcome.issues.end() &&
               outcome.per_take.empty() && !std::filesystem::exists(paths.root / "damaged"),
           "D-2, RF-2.2: a damaged initial state is rejected before admission");
}

// BR-067: the token sends `cancel` while the first take runs.
void cancellation_reaches_the_runtime(const Paths &paths) {
    Recorder observer;
    qa::CancelToken cancel;
    observer.on_phase_hook = [&cancel](const qa::RequestPhase &phase) {
        if (phase.kind == qa::RequestPhaseKind::running)
            cancel.request();
    };
    const auto outcome =
        qa::run_check(request(paths, "cancel", {paths.long_take.string(), paths.long_take.string()},
                              "runner-cancel"),
                      observer, cancel);
    const auto *active = ran(outcome, 0);
    const auto *next = pending(outcome, 1);
    expect(active != nullptr && active->playback.kind == qa::PlaybackKind::cancelled,
           "RF-2.5: the active take ends cancelled");
    // BR-076 (RF-2.13): the evidence of the cancelled take names the parts it lacks.
    expect(active != nullptr && !active->evidence.complete &&
               std::find(active->evidence.reasons.begin(), active->evidence.reasons.end(),
                         "data_lost") != active->evidence.reasons.end(),
           "RF-2.13: a cancelled take is incomplete(data_lost)");
    expect(next != nullptr && next->reason == "not_started_after_cancellation",
           "RF-2.5: the pending take is not started");
    // The Runtime itself stopped: it received `cancel` before its last frame.
    expect(observer.reported("status=replay_cancelled") &&
               !observer.reported("inputs_consumed=600 "),
           "RF-2.5: the Runtime receives cancel over the control channel and stops");
    if (!observer.reported("status=replay_cancelled"))
        for (const auto &line : observer.lines)
            std::cerr << "  " << line << '\n';
    const auto run = ledger_run(paths.root / "cancel", "runner-cancel");
    expect(run && run->playback_result == qa::PlaybackResult::cancelled && run->cessation_confirmed,
           "RNF-5: the cancelled request ceased with confirmation");
}

// BR-060, BR-072: a take changed after the validation is never handed to the Runtime.
void changed_material_stops_its_take(const Paths &paths) {
    const auto copy = paths.root / "second.arp";
    std::filesystem::copy_file(paths.take, copy, std::filesystem::copy_options::overwrite_existing);
    Recorder observer;
    observer.on_outcome_hook = [&copy](const qa::TakeOutcome &outcome) {
        if (outcome.position == 0) {
            std::ofstream output{copy, std::ios::binary | std::ios::app};
            output << "changed";
        }
    };
    qa::CancelToken cancel;
    const auto outcome = qa::run_check(
        request(paths, "changed", {paths.take.string(), copy.string()}, "runner-changed"), observer,
        cancel);
    const auto *second = ran(outcome, 1);
    expect(second != nullptr && second->playback.kind == qa::PlaybackKind::failed &&
               second->playback.diagnostic == "material_changed",
           "RF-2.11: the changed take stops with material_changed");
    expect(observer.reported("material_changed: --take[1]"),
           "RF-2.11: the change is reported in its field");
}

// BR-068: a run an interrupted supervisor left open is recovered as incomplete.
void interrupted_run_is_recovered(const Paths &paths) {
    const auto output = paths.root / "recovery";
    std::filesystem::create_directories(output);
    {
        auto opened = qa::open_request_ledger(output);
        auto *ledger = std::get_if<qa::RequestLedger>(&opened);
        expect(ledger != nullptr, "the ledger opens");
        if (ledger == nullptr)
            return;
        qa::Request left{"left-open", "audio-qa-check-v1",    "conditions-left",
                         {"t.ayr"},   qa::Admission::pending, std::nullopt};
        qa::Run run;
        run.run_id = "run-left";
        run.request_id = "left-open";
        run.take_id = "t.ayr";
        run.phase = qa::Phase::playing;
        run.playback_result = qa::PlaybackResult::in_progress;
        (void)ledger->register_request(left, run);
    }
    Recorder observer;
    qa::CancelToken cancel;
    const auto outcome = qa::run_check(
        request(paths, "recovery", {paths.take.string()}, "runner-after-cut"), observer, cancel);
    const auto recovered = ledger_run(output, "left-open");
    expect(observer.reported("run_recovered: left-open run-left") && recovered &&
               recovered->phase == qa::Phase::closed &&
               recovered->evidence_result == qa::EvidenceResult::incomplete &&
               !recovered->cessation_confirmed,
           "RNF-5: the open run is closed as incomplete, its cessation unconfirmed");
    std::error_code ignored;
    expect(!std::filesystem::exists(output / "runs" / "run-left", ignored),
           "RNF-5: the interrupted take is not relaunched");
    expect(outcome.exit_code == 0, "RNF-5: the next request runs normally");
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 8) {
        std::cerr << "usage: check_runner_integration_test runtime core rom take long-take "
                     "rejected-state-take root\n";
        return 2;
    }
    const Paths paths{argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]};
    std::error_code ignored;
    std::filesystem::remove_all(paths.root, ignored);
    std::filesystem::create_directories(paths.root);
    phases_in_order(paths);
    failure_stops_the_request(paths);
    damaged_state_is_rejected_before_admission(paths);
    cancellation_reaches_the_runtime(paths);
    changed_material_stops_its_take(paths);
    interrupted_run_is_recovered(paths);
    if (failures != 0)
        return 1;
    std::cout << "run_check runs, stops, cancels and recovers requests\n";
    return 0;
}
