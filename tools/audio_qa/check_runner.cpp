#include "check_runner.h"

#include "check_admission.h"
#include "check_execution.h"
#include "check_summary.h"
#include "evidence_completeness.h"
#include "inspection_evidence.h"
#include "inspection_facts.h"
#include "integrated_evidence.h"
#include "play_launch_manifest.h"
#include "reference_store.h"
#include "request_ledger.h"
#include "request_preflight.h"
#include "request_summary_store.h"
#include "request_terminal_arbiter.h"
#include "run_recovery.h"
#include "session_occupancy.h"
#include "take_plan.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>
#include <variant>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace ayther::audio_qa {

void CheckObserver::on_report(std::string_view) {}
void CheckObserver::on_message(CheckMessage) {}

namespace {

constexpr int incomplete_exit_code = 2;
constexpr int invalid_exit_code = 3;
constexpr int evidence_failure_exit_code = 4;

std::string digest(const ContentIdentity &identity) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : identity.sha256)
        output << std::setw(2) << static_cast<unsigned>(byte);
    return output.str();
}

// Spec 002 (RNF-5): one supervisor per destination. The lock is held for the whole
// request; whoever holds it knows that any run the ledger still has open was left by an
// interrupted supervisor.
class DestinationLock final {
  public:
    explicit DestinationLock(const std::filesystem::path &output) {
        const auto path = output / ".ayther-qa-check.lock";
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
#else
        descriptor_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (descriptor_ >= 0 && ::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            ::close(descriptor_);
            descriptor_ = -1;
        }
#endif
    }
    ~DestinationLock() {
#ifdef _WIN32
        if (held())
            CloseHandle(handle_);
#else
        if (descriptor_ >= 0)
            ::close(descriptor_);
#endif
    }
    DestinationLock(const DestinationLock &) = delete;
    DestinationLock &operator=(const DestinationLock &) = delete;

    [[nodiscard]] bool held() const noexcept {
#ifdef _WIN32
        return handle_ != INVALID_HANDLE_VALUE;
#else
        return descriptor_ >= 0;
#endif
    }

  private:
#ifdef _WIN32
    HANDLE handle_{INVALID_HANDLE_VALUE};
#else
    int descriptor_{-1};
#endif
};

struct Prepared {
    std::optional<EffectiveRequest> effective;
    std::vector<FieldIssue> issues;
};

// RF-1.1, RF-1.6, RF-1.7: auxiliary sources are optional and read-only; every effective
// value keeps the place it came from.
Prepared prepare(const ReplayRequest &request) {
    Prepared prepared;
    ConditionSources sources;
    if (!request.play_manifest.empty()) {
        const auto loaded =
            load_play_launch_manifest(request.play_manifest, "qa-check-baseline",
                                      "qa-check-execution", "qa-check-play-manifest");
        if (const auto *manifest = std::get_if<Reference>(&loaded))
            sources.play_manifest = *manifest;
        else
            prepared.issues.push_back({"--play-manifest", "play_manifest_unavailable"});
    }
    if (!request.reference.empty()) {
        const auto stored = read_reference(request.reference);
        if (const auto *reference = std::get_if<StoredReference>(&stored))
            sources.reference = reference->reference;
        else
            prepared.issues.push_back({"--reference", "reference_unavailable"});
    }
    if (!prepared.issues.empty())
        return prepared;
    if (request.core.empty())
        if (const auto play_config = default_play_config_path())
            sources.environment_core = read_play_config_core(*play_config, request.rom);
    auto resolved = resolve_effective_request(request, sources);
    if (auto *issues = std::get_if<std::vector<EffectiveIssue>>(&resolved)) {
        prepared.issues = std::move(*issues);
        return prepared;
    }
    prepared.effective = std::get<EffectiveRequest>(std::move(resolved));
    return prepared;
}

// The live view of one take and its confirmed outcome, built from what the Runtime and
// the evidence report (RF-2.4).
struct TakeRecord {
    TakeOutcome outcome;
    TakeTechnicalResult technical;
    StoredTakeSummary stored;
};

std::string evidence_code(const EvidenceOutcome &evidence) {
    return evidence.complete ? "complete" : "incomplete";
}

class RequestRun final {
  public:
    RequestRun(const ReplayRequest &request, CheckObserver &observer, CancelToken &cancel)
        : request_(request), observer_(observer), cancel_(cancel) {}

    RequestOutcome run();

  private:
    void report(std::string_view line) { observer_.on_report(line); }
    [[nodiscard]] CheckLanguage language() const noexcept {
        return request_.language == "en" ? CheckLanguage::english : CheckLanguage::spanish;
    }
    // RNF-7: the localized text of a code, after the stable code line.
    void localized(std::string_view code, std::string_view subject) {
        const auto text = check_issue_message(language(), code);
        if (!text)
            return;
        std::string line{"audio_qa_message["};
        line += check_language_code(language());
        line += "]: ";
        if (!subject.empty()) {
            line += subject;
            line += ": ";
        }
        line += *text;
        report(line);
    }
    void error(std::string_view code, std::string_view detail = {}) {
        std::string line{"audio_qa_error: "};
        line += code;
        if (!detail.empty()) {
            line += ": ";
            line += detail;
        }
        report(line);
        localized(code, detail);
    }
    RequestOutcome &finish(int exit_code, std::optional<CheckMessage> message) {
        outcome_.exit_code = exit_code;
        if (message)
            observer_.on_message(*message);
        observer_.on_phase({RequestPhaseKind::closed, 0});
        return outcome_;
    }
    RequestOutcome &reject(std::vector<FieldIssue> issues) {
        for (const auto &issue : issues)
            error(issue.code, issue.field);
        outcome_.issues = std::move(issues);
        return finish(invalid_exit_code, CheckMessage::invalid_request);
    }

    void recover_open_runs(RequestLedger &ledger);
    RequestOutcome &known(const Run &run);
    TakeRecord run_take(const EffectiveRequest &effective, const PreflightPins &pins,
                        const Request &admitted_request, const TakeSlot &slot,
                        const std::string &run_id, RequestTerminalArbiter &arbiter,
                        bool &cessation_confirmed, std::optional<std::uint64_t> &last_frame,
                        bool last_take);

    const ReplayRequest &request_;
    CheckObserver &observer_;
    CancelToken &cancel_;
    RequestOutcome outcome_;
    std::filesystem::path output_;
};

// RNF-5: a run that an interrupted supervisor left open is recovered as incomplete and
// closed, never relaunched. Its cessation stays unconfirmed: nobody saw the process end.
void RequestRun::recover_open_runs(RequestLedger &ledger) {
    std::vector<Run> open;
    for (const auto &entry : ledger.entries())
        if (entry.run.phase != Phase::closed)
            open.push_back(entry.run);
    for (auto run : open) {
        const auto recovered = recover_interrupted_run(
            ledger, run.request_id, output_ / "runs" / run.run_id / "checkpoints" / "latest");
        if (const auto *interrupted = std::get_if<InterruptedRunRecovery>(&recovered))
            run = interrupted->run;
        run.phase = Phase::closed;
        run.evidence_result = EvidenceResult::incomplete;
        if (run.playback_result == PlaybackResult::in_progress ||
            run.playback_result == PlaybackResult::not_started)
            run.playback_result = PlaybackResult::error;
        run.cessation_confirmed = false;
        (void)ledger.update_run(run);
        std::string line{"audio_qa_status: run_recovered: "};
        line += run.request_id;
        line += ' ';
        line += run.run_id;
        line += " evidence=incomplete reason=unconfirmed_after_crash";
        report(line);
    }
}

// RF-2.3: the same request returns its confirmed summary as it is, without running.
RequestOutcome &RequestRun::known(const Run &run) {
    std::string status{"audio_qa_status: request_known: "};
    status += outcome_.request_id;
    report(status);
    outcome_.known = true;
    const auto stored = read_request_summary(request_summary_path(output_, run.run_id));
    if (const auto *document = std::get_if<RequestSummaryDocument>(&stored)) {
        const auto summary = technical_summary(*document);
        if (!summary) {
            error("request_summary_unreadable");
            return finish(evidence_failure_exit_code, CheckMessage::technical_summary_invalid);
        }
        for (const auto &take : document->takes) {
            if (take.playback == "not_started") {
                outcome_.per_take.emplace_back(
                    NotStartedTake{take.position, take.take, take.diagnostic});
                continue;
            }
            TakeOutcome value;
            value.position = take.position;
            value.take = take.take;
            value.run_id = take.run_id;
            value.playback = {parse_playback_kind(take.playback).value_or(PlaybackKind::failed),
                              take.diagnostic};
            value.traversal = parse_traversal_kind(take.traversal);
            value.evidence = {take.evidence == "complete", take.evidence_reasons};
            value.linear_completed = value.traversal == TraversalKind::linear &&
                                     value.playback.kind == PlaybackKind::natural_end &&
                                     value.evidence.complete;
            outcome_.per_take.emplace_back(std::move(value));
        }
        outcome_.confirmed = document->confirmed;
        outcome_.linear_complete = document->linear_complete;
        report(format_check_summary(*summary));
        return finish(document->exit_code, CheckMessage::request_known);
    }
    if (std::get<RequestSummaryReadError>(stored) != RequestSummaryReadError::missing) {
        error("request_summary_unreadable");
        return finish(evidence_failure_exit_code, CheckMessage::technical_summary_invalid);
    }
    // Admitted but never confirmed. A closed run was recovered as incomplete and is not
    // relaunched (RNF-5); an open one belongs to the supervisor that holds the destination.
    report(run.phase == Phase::closed ? "audio_qa_status: request_unconfirmed: evidence=incomplete"
                                      : "audio_qa_status: request_in_progress: " + run.run_id);
    return finish(incomplete_exit_code, CheckMessage::request_known);
}

TakeRecord RequestRun::run_take(const EffectiveRequest &effective, const PreflightPins &pins,
                                const Request &admitted_request, const TakeSlot &slot,
                                const std::string &run_id, RequestTerminalArbiter &arbiter,
                                bool &cessation_confirmed, std::optional<std::uint64_t> &last_frame,
                                const bool last_take) {
    TakeRecord record;
    auto &take = record.outcome;
    take.position = slot.position;
    take.take = slot.path;
    take.run_id = run_id;
    const auto take_pin =
        std::find_if(pins.takes.begin(), pins.takes.end(),
                     [&slot](const auto &pin) { return pin.position == slot.position; });
    if (take_pin != pins.takes.end())
        record.stored.sha256 = stored_material(*take_pin).sha256;
    std::string diagnostic;
    bool launched{};

    // RF-2.11: the materials of this take stay open for reading only while it runs; one
    // that changed since the validation stops the request before the Runtime receives it.
    const auto held = hold_materials(take_material_pins(pins, slot.position));
    if (const auto *changed = std::get_if<FieldIssue>(&held)) {
        error(changed->code, changed->field);
        diagnostic = changed->code;
        (void)arbiter.fail(changed->code);
        take.evidence = {false, {changed->code}};
    } else {
        auto take_request = admitted_request;
        take_request.take_ids = {slot.path};
        const ReplayExecutionControl control{
            &cancel_,
            [this, &slot] { observer_.on_phase({RequestPhaseKind::running, slot.position}); },
            [this](const ReplayStateView &state) { observer_.on_replay_state(state); },
            slot.position, last_take};
        ReplayCessation cessation;
        const auto executed =
            execute_check_replay(effective, take_request, run_id, control, cessation);
        launched = cessation.launched;
        cessation_confirmed = cessation_confirmed && cessation.confirmed();
        if (const auto *evidence = std::get_if<CheckExecutionEvidence>(&executed)) {
            const auto *preserved = evidence->preserved ? &*evidence->preserved : nullptr;
            const auto &replay = evidence->replay;
            if (replay.inputs_consumed > 0U)
                last_frame = replay.inputs_consumed - 1U;
            std::ostringstream line;
            line << "audio_qa_replay: run_id=" << replay.run_id << " take=" << replay.take_id
                 << " position=" << slot.position << " recording_frames=" << replay.recording_frames
                 << " inputs_consumed=" << replay.inputs_consumed
                 << " assignments=" << replay.assignment_count
                 << " initial_state_sha256=" << digest(replay.initial_game_state)
                 << " final_state_sha256=" << digest(replay.final_game_state)
                 << " trace_facts=" << replay.trace.observed_fact_count
                 << " occurrence=" << replay.trace.occurrence
                 << " ingress=" << replay.trace.ingress.producer << ':'
                 << replay.trace.ingress.sequence
                 << " candidate=" << replay.trace.candidate.producer << ':'
                 << replay.trace.candidate.sequence
                 << " selection=" << replay.trace.selection.producer << ':'
                 << replay.trace.selection.sequence
                 << " request=" << replay.trace.playback_request.producer << ':'
                 << replay.trace.playback_request.sequence
                 << " decision=" << replay.trace.playback_decision.producer << ':'
                 << replay.trace.playback_decision.sequence
                 << " effect=" << replay.trace.playback_effect.producer << ':'
                 << replay.trace.playback_effect.sequence
                 << " mix_span=" << replay.trace.mix_span.producer << ':'
                 << replay.trace.mix_span.sequence;
            if (preserved != nullptr)
                line << " durable_facts=" << preserved->facts
                     << " durable_pcm_blocks=" << preserved->pcm_blocks
                     << " durable_pcm_bytes=" << preserved->pcm_bytes << " fact_integrity_complete="
                     << (preserved->fact_integrity_complete ? "true" : "false")
                     << " relationships_reopened="
                     << (preserved->relationships_reopened ? "true" : "false")
                     << " runtime_data_isolated="
                     << (evidence->runtime_data_isolated ? "true" : "false");
            else if (evidence->preservation_error)
                line << " evidence_error="
                     << integrated_evidence_error_code(*evidence->preservation_error);
            bool evidence_complete =
                replay.succeeded && preserved != nullptr &&
                (replay.assignment_count == 0U || preserved->relationships_reopened);
            line << " presentation=" << replay.presentation.mode
                 << " presentation_status=" << replay.presentation.code
                 << " presented_frames=" << replay.presentation.presented_frames
                 << " affected_frames=" << replay.presentation.affected_frames
                 << " audio_backend=" << replay.presentation.audio_backend
                 << " status=" << (evidence_complete ? "replay_evidence_reopened" : replay.code);
            report(line.str());

            // The Runtime says how the playback ended (terminal 1.4); an older one does not,
            // and its outcome is derived from its code.
            const auto reported =
                replay.playback ? parse_playback_kind(*replay.playback) : std::nullopt;
            const auto playback = reported.value_or(
                replay.code == "replay_cancelled" ? PlaybackKind::cancelled
                : replay.inputs_consumed == replay.recording_frames && replay.recording_frames > 0U
                    ? PlaybackKind::natural_end
                    : PlaybackKind::failed);
            if (cancel_.requested() || playback == PlaybackKind::cancelled)
                (void)arbiter.cancel();
            if (playback == PlaybackKind::natural_end)
                (void)arbiter.natural_end();
            else if (playback == PlaybackKind::failed || playback == PlaybackKind::interrupted)
                (void)arbiter.fail(replay.code, playback == PlaybackKind::interrupted);
            take.traversal =
                replay.traversal ? parse_traversal_kind(*replay.traversal) : std::nullopt;
            // BR-076 (contracts.md C2): complete only with every part kept and checked.
            const auto run_directory = output_ / "runs" / run_id;
            std::error_code ignored;
            EvidenceParts parts;
            parts.identification =
                take_pin != pins.takes.end() && !run_id.empty() && replay.run_id == run_id;
            parts.conditions = !admitted_request.conditions_id.empty();
            parts.positions = std::holds_alternative<TraversalDocument>(
                read_traversal(run_directory / "traversal.toml"));
            parts.controls = std::holds_alternative<RunInspectionFacts>(
                read_run_inspection_facts(run_directory));
            parts.result =
                std::filesystem::is_regular_file(run_directory / "replay-result.toml", ignored);
            parts.data_without_losses = evidence_complete && replay.trace.loss_free;
            parts.fragments_flushed = preserved != nullptr && preserved->fact_integrity_complete;
            const auto evaluated = evaluate_evidence(parts);
            // The Runtime reasons first, then the parts that are missing.
            take.evidence.reasons = replay.evidence_reasons;
            if (!replay.succeeded &&
                std::find(take.evidence.reasons.begin(), take.evidence.reasons.end(),
                          replay.code) == take.evidence.reasons.end())
                take.evidence.reasons.push_back(replay.code);
            for (const auto &reason : evaluated.reasons)
                if (std::find(take.evidence.reasons.begin(), take.evidence.reasons.end(), reason) ==
                    take.evidence.reasons.end())
                    take.evidence.reasons.push_back(reason);
            take.evidence.complete = evaluated.complete;
            evidence_complete = evaluated.complete;
            diagnostic = evidence_complete
                             ? "replay_evidence_complete"
                             : (replay.succeeded ? "evidence_reopen_failed" : replay.code);
        } else {
            const auto code =
                std::string{check_execution_error_code(std::get<CheckExecutionError>(executed))};
            error(code, slot.path);
            if (cancel_.requested())
                (void)arbiter.cancel();
            (void)arbiter.fail(code);
            diagnostic = code;
            take.evidence = {false, {code}};
        }
    }

    const auto confirmed =
        arbiter.confirm().value_or(PlaybackOutcome{PlaybackKind::failed, diagnostic});
    take.playback = confirmed;
    if (take.playback.kind != PlaybackKind::natural_end && take.playback.diagnostic.empty() &&
        take.playback.kind != PlaybackKind::cancelled)
        take.playback.diagnostic = diagnostic;
    take.linear_completed = take.traversal == TraversalKind::linear &&
                            take.playback.kind == PlaybackKind::natural_end &&
                            take.evidence.complete;
    const bool complete = take.playback.kind == PlaybackKind::natural_end && take.evidence.complete;
    auto &technical = record.technical;
    technical.take_id = slot.path;
    technical.outcome =
        complete ? CheckTechnicalOutcome::complete : CheckTechnicalOutcome::incomplete;
    technical.diagnostic_code =
        diagnostic.empty() ? std::string{"replay_evidence_complete"} : diagnostic;
    technical.position = slot.position;
    technical.playback = playback_kind_code(take.playback.kind);
    technical.traversal = take.traversal ? std::string{traversal_kind_code(*take.traversal)}
                                         : std::string{launched ? "unknown" : "none"};
    technical.evidence = evidence_code(take.evidence);
    auto &stored = record.stored;
    stored.position = slot.position;
    stored.take = slot.path;
    stored.run_id = run_id;
    stored.outcome = check_technical_outcome_code(technical.outcome);
    stored.diagnostic = technical.diagnostic_code;
    stored.playback = technical.playback;
    stored.traversal = technical.traversal;
    stored.evidence = technical.evidence;
    stored.evidence_reasons = take.evidence.reasons;
    return record;
}

RequestOutcome RequestRun::run() {
    observer_.on_phase({RequestPhaseKind::validating, 0});
    auto prepared = prepare(request_);
    if (!prepared.effective)
        return reject(std::move(prepared.issues));
    auto &effective = *prepared.effective;
    outcome_.request_id =
        request_.request_id.empty() ? generate_check_id("request-") : request_.request_id;
    if (request_.request_id.empty()) {
        effective.request_id = outcome_.request_id;
        effective.values.push_back({"request_id", outcome_.request_id, ValueSource::generated});
    }
    for (const auto &value : effective.values) {
        std::string line{"audio_qa_effective: "};
        line += value.key;
        line += '=';
        line += value.value;
        line += " source=";
        line += value_source_code(value.source);
        report(line);
    }
    // RF-1.2, RF-2.2, plan §5.2: every material, the takes, the core with the ROM and the
    // pack are validated before admission; nothing is created in the destination.
    auto checked = preflight_request(effective);
    if (!checked.issues.empty())
        return reject(std::move(checked.issues));
    const auto &pins = *checked.pins;

    output_ = request_.output;
    std::error_code directory_error;
    std::filesystem::create_directories(output_, directory_error);
    if (directory_error || !std::filesystem::is_directory(output_)) {
        error("output_root_unavailable");
        return finish(evidence_failure_exit_code, CheckMessage::output_root_unavailable);
    }
    const DestinationLock lock{output_};
    auto ledger_result = open_request_ledger(output_);
    auto *ledger = std::get_if<RequestLedger>(&ledger_result);
    if (ledger == nullptr) {
        error("request_ledger_unavailable");
        return finish(evidence_failure_exit_code, CheckMessage::request_ledger_unavailable);
    }
    if (lock.held())
        recover_open_runs(*ledger);
    SessionOccupancy occupancy;
    if (!restore_check_occupancy(*ledger, occupancy)) {
        error("request_ledger_has_multiple_active_runs");
        return finish(evidence_failure_exit_code, CheckMessage::request_ledger_inconsistent);
    }

    const auto materials = material_pins(pins);
    auto draft =
        make_check_request(effective, materials, outcome_.request_id, generate_check_id("run-"));
    const auto admitted_request = draft.request;
    const auto admission = admit_check_request(*ledger, occupancy, std::move(draft));
    if (std::holds_alternative<RequestLedgerError>(admission)) {
        error("request_ledger_publish_failed");
        return finish(evidence_failure_exit_code, CheckMessage::request_ledger_publish_failed);
    }
    const auto &admitted = std::get<CheckAdmissionResult>(admission);
    switch (admitted.decision) {
    case CheckAdmissionDecision::accepted:
        report("audio_qa_status: request_accepted: " + outcome_.request_id);
        observer_.on_message(CheckMessage::request_accepted);
        break;
    case CheckAdmissionDecision::known:
        return known(admitted.run);
    case CheckAdmissionDecision::identity_conflict:
        error("request_identity_conflict", outcome_.request_id);
        return finish(invalid_exit_code, CheckMessage::request_identity_conflict);
    case CheckAdmissionDecision::busy:
        error("session_busy", admitted.active_request_id + ':' + admitted.active_run_id);
        return finish(invalid_exit_code, CheckMessage::session_busy);
    case CheckAdmissionDecision::invalid:
        error("invalid_request");
        return finish(invalid_exit_code, CheckMessage::invalid_request);
    case CheckAdmissionDecision::capacity_exceeded:
        error("request_capacity_exceeded");
        return finish(invalid_exit_code, CheckMessage::request_capacity_exceeded);
    }
    observer_.on_phase({RequestPhaseKind::admitted, 0});

    // RF-1.4, RF-2.5, plan §5.3: takes in order; a cancellation or a failure stops the
    // request and the takes that did not start say why.
    RequestTerminalArbiter arbiter;
    const auto slots = plan_takes(effective.takes);
    std::vector<TakeTechnicalResult> results;
    std::vector<StoredTakeSummary> stored;
    bool cessation_confirmed = true;
    std::optional<std::uint64_t> last_frame;
    for (const auto &slot : slots) {
        if (cancel_.requested())
            (void)arbiter.cancel();
        if (arbiter.stop_requested()) {
            const std::string reason{arbiter.not_started_reason()};
            NotStartedTake pending{slot.position, slot.path, reason};
            outcome_.per_take.emplace_back(pending);
            TakeTechnicalResult technical;
            technical.take_id = slot.path;
            technical.outcome = CheckTechnicalOutcome::incomplete;
            technical.diagnostic_code = reason;
            technical.position = slot.position;
            technical.playback = "not_started";
            technical.traversal = "none";
            technical.evidence = "incomplete";
            results.push_back(technical);
            StoredTakeSummary summary;
            summary.position = slot.position;
            summary.take = slot.path;
            summary.outcome = "incomplete";
            summary.diagnostic = reason;
            summary.playback = "not_started";
            summary.traversal = "none";
            summary.evidence = "incomplete";
            stored.push_back(std::move(summary));
            continue;
        }
        observer_.on_phase({RequestPhaseKind::preparing, slot.position});
        arbiter.begin_take(slot.position);
        const auto run_id = run_id_for_position(admitted.run.run_id, slot.position);
        auto record = run_take(effective, pins, admitted_request, slot, run_id, arbiter,
                               cessation_confirmed, last_frame, &slot == &slots.back());
        observer_.on_take_outcome(record.outcome);
        outcome_.per_take.emplace_back(record.outcome);
        results.push_back(std::move(record.technical));
        stored.push_back(std::move(record.stored));
    }

    observer_.on_phase({RequestPhaseKind::closing, 0});
    const auto summary = summarize_check_results(results);
    if (!summary) {
        error("technical_summary_invalid");
        return finish(evidence_failure_exit_code, CheckMessage::technical_summary_invalid);
    }
    const bool any_cancelled =
        std::any_of(results.begin(), results.end(),
                    [](const TakeTechnicalResult &take) { return take.playback == "cancelled"; });
    const bool all_natural =
        std::all_of(results.begin(), results.end(),
                    [](const TakeTechnicalResult &take) { return take.playback == "natural_end"; });
    auto final_run = admitted.run;
    final_run.phase = Phase::closed;
    final_run.playback_result = any_cancelled ? PlaybackResult::cancelled
                                : all_natural ? PlaybackResult::natural_end
                                              : PlaybackResult::error;
    final_run.evidence_result =
        summary->complete == results.size() ? EvidenceResult::complete : EvidenceResult::incomplete;
    if (all_natural || any_cancelled)
        final_run.last_executed_frame = last_frame;
    // RNF-5: confirmed only when every process exited and every evidence was closed.
    final_run.cessation_confirmed = cessation_confirmed;
    const auto ledger_update = ledger->update_run(final_run);
    const auto *updated = std::get_if<bool>(&ledger_update);
    if (updated == nullptr || !*updated) {
        error("request_ledger_publish_failed");
        return finish(evidence_failure_exit_code, CheckMessage::request_ledger_publish_failed);
    }

    // RF-2.3, RNF-5: the confirmed summary is durable before it is reported.
    RequestSummaryDocument document;
    document.request_id = outcome_.request_id;
    document.conditions_id = admitted_request.conditions_id;
    document.run_id = admitted.run.run_id;
    document.exit_code = summary->exit_code;
    document.linear_complete = summary->linear_complete;
    document.values = effective.values;
    for (const auto &pin : materials)
        document.materials.push_back(stored_material(pin));
    document.takes = std::move(stored);
    if (!std::holds_alternative<DurablePublishedFile>(
            write_request_summary(request_summary_path(output_, admitted.run.run_id), document))) {
        error("request_summary_publish_failed");
        return finish(evidence_failure_exit_code, CheckMessage::request_ledger_publish_failed);
    }
    outcome_.confirmed = true;
    outcome_.linear_complete = summary->linear_complete;
    report(format_check_summary(*summary));
    for (const auto &take : summary->takes)
        localized(take.diagnostic_code, "take[" + std::to_string(take.position) + "]");
    return finish(summary->exit_code, summary->exit_code == 0 ? CheckMessage::summary_complete
                                      : summary->exit_code == evidence_failure_exit_code
                                          ? CheckMessage::summary_preservation_failure
                                      : summary->cancelled > 0U ? CheckMessage::summary_cancelled
                                                                : CheckMessage::summary_incomplete);
}

} // namespace

std::span<const OptionDescriptor> option_descriptors() noexcept {
    return check_option_descriptors();
}

PreflightResult preflight(const ReplayRequest &request) {
    PreflightResult result;
    auto prepared = prepare(request);
    if (!prepared.effective) {
        result.issues = std::move(prepared.issues);
        return result;
    }
    result.effective = prepared.effective->values;
    result.issues = preflight_request(*prepared.effective).issues;
    return result;
}

RequestOutcome run_check(const ReplayRequest &request, CheckObserver &observer,
                         CancelToken &cancel) {
    return RequestRun{request, observer, cancel}.run();
}

} // namespace ayther::audio_qa
