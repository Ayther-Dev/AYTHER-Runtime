#include "campaign_audit.h"
#include "check_admission.h"
#include "check_execution.h"
#include "check_messages.h"
#include "check_options.h"
#include "check_profile.h"
#include "check_summary.h"
#include "console_interrupt.h"
#include "evidence_query_index.h"
#include "integrated_evidence.h"
#include "query_options.h"

#include <charconv>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr int incomplete_exit_code = 2;
constexpr int invalid_invocation_exit_code = 3;
constexpr int evidence_failure_exit_code = 4;

void emit_user_message(ayther::audio_qa::CheckLanguage language,
                       ayther::audio_qa::CheckMessage message) {
    std::cerr << "audio_qa_message[" << ayther::audio_qa::check_language_code(language)
              << "]: " << ayther::audio_qa::check_message(language, message) << '\n';
}

int run_query(std::span<const std::string_view> arguments) {
    const auto parsed = ayther::audio_qa::parse_query_options(arguments);
    const auto *options = std::get_if<ayther::audio_qa::QueryOptions>(&parsed);
    if (options == nullptr) {
        const auto &error = std::get<ayther::audio_qa::CheckOptionError>(parsed);
        std::cerr << "audio_qa_error: " << ayther::audio_qa::check_option_error_code(error.code);
        if (!error.option.empty())
            std::cerr << ": " << error.option;
        std::cerr << '\n';
        emit_user_message(ayther::audio_qa::CheckLanguage::spanish,
                          ayther::audio_qa::CheckMessage::invalid_options);
        return invalid_invocation_exit_code;
    }
    const auto language = options->language == "en" ? ayther::audio_qa::CheckLanguage::english
                                                    : ayther::audio_qa::CheckLanguage::spanish;
    const auto opened = ayther::audio_qa::read_evidence_query_index(options->index);
    const auto *index = std::get_if<ayther::audio_qa::EvidenceQueryIndex>(&opened);
    if (index == nullptr) {
        std::cerr << "audio_qa_error: evidence_query_index_unavailable\n";
        emit_user_message(language, ayther::audio_qa::CheckMessage::query_index_unavailable);
        return evidence_failure_exit_code;
    }
    const ayther::audio_qa::FactId origin{options->run_id, options->producer_id, options->sequence};
    const auto routes = ayther::audio_qa::query_evidence_index(*index, origin);
    std::cout << ayther::audio_qa::format_evidence_query(origin, routes) << '\n';
    if (routes.empty()) {
        emit_user_message(language, ayther::audio_qa::CheckMessage::query_not_found);
        return incomplete_exit_code;
    }
    emit_user_message(language, ayther::audio_qa::CheckMessage::query_found);
    return 0;
}

int run_audit(const std::span<const std::string_view> arguments) {
    std::string run_directory;
    std::string run_id;
    std::string expected_text;
    std::string language{"es"};
    for (std::size_t index{}; index < arguments.size(); ++index) {
        const auto option = arguments[index];
        if (index + 1U >= arguments.size()) {
            std::cerr << "audio_qa_error: invalid_audit_options\n";
            return invalid_invocation_exit_code;
        }
        const auto value = arguments[++index];
        std::string *destination{};
        if (option == "--run-directory")
            destination = &run_directory;
        else if (option == "--run-id")
            destination = &run_id;
        else if (option == "--expected-assignments")
            destination = &expected_text;
        else if (option == "--language")
            destination = &language;
        else {
            std::cerr << "audio_qa_error: invalid_audit_option: " << option << '\n';
            return invalid_invocation_exit_code;
        }
        if (!destination->empty() && destination != &language) {
            std::cerr << "audio_qa_error: duplicate_audit_option: " << option << '\n';
            return invalid_invocation_exit_code;
        }
        destination->assign(value);
    }
    std::uint64_t expected{};
    const auto parsed = std::from_chars(expected_text.data(),
                                        expected_text.data() + expected_text.size(), expected);
    if (run_directory.empty() || run_id.empty() || expected_text.empty() ||
        parsed.ec != std::errc{} || parsed.ptr != expected_text.data() + expected_text.size() ||
        (language != "es" && language != "en")) {
        std::cerr << "audio_qa_error: invalid_audit_options\n";
        return invalid_invocation_exit_code;
    }
    const auto audited = ayther::audio_qa::audit_campaign_evidence(
        std::filesystem::path{run_directory}, run_id, expected);
    const auto *summary = std::get_if<ayther::audio_qa::CampaignAuditSummary>(&audited);
    if (summary == nullptr) {
        std::cerr << "audio_qa_error: campaign_audit_"
                  << ayther::audio_qa::campaign_audit_error_code(
                         std::get<ayther::audio_qa::CampaignAuditError>(audited))
                  << '\n';
        std::cerr << "audio_qa_message[" << language << "]: "
                  << (language == "es" ? "No se pudo reabrir y auditar la evidencia."
                                       : "The evidence could not be reopened and audited.")
                  << '\n';
        return evidence_failure_exit_code;
    }
    std::cout << ayther::audio_qa::format_campaign_audit(*summary) << '\n';
    std::cerr << "audio_qa_message[" << language << "]: "
              << (summary->complete
                      ? (language == "es" ? "La evidencia de la campaña está completa."
                                          : "The campaign evidence is complete.")
                      : (language == "es" ? "La evidencia de la campaña está incompleta."
                                          : "The campaign evidence is incomplete."))
              << '\n';
    return summary->complete ? 0 : incomplete_exit_code;
}

std::string digest(const ayther::audio_qa::ContentIdentity &identity) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : identity.sha256)
        output << std::setw(2) << static_cast<unsigned>(byte);
    return output.str();
}

} // namespace

int main(int argc, char **argv) {
    if (argc >= 2 && std::string_view{argv[1]} == "audit") {
        std::vector<std::string_view> audit_arguments;
        audit_arguments.reserve(static_cast<std::size_t>(argc - 2));
        for (int index = 2; index < argc; ++index)
            audit_arguments.emplace_back(argv[index] != nullptr ? argv[index] : "");
        return run_audit(audit_arguments);
    }
    if (argc >= 2 && std::string_view{argv[1]} == "query") {
        std::vector<std::string_view> query_arguments;
        query_arguments.reserve(static_cast<std::size_t>(argc - 2));
        for (int index = 2; index < argc; ++index)
            query_arguments.emplace_back(argv[index] != nullptr ? argv[index] : "");
        return run_query(query_arguments);
    }
    if (argc < 2 || std::string_view{argv[1]} != "check") {
        std::cerr << "audio_qa_error: invalid_invocation: expected command 'check'\n";
        emit_user_message(ayther::audio_qa::CheckLanguage::spanish,
                          ayther::audio_qa::CheckMessage::invalid_invocation);
        return invalid_invocation_exit_code;
    }

    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc - 2));
    for (int index = 2; index < argc; ++index)
        arguments.emplace_back(argv[index] != nullptr ? argv[index] : "");

    const auto parsed = ayther::audio_qa::parse_check_options(arguments);
    if (const auto *error = parsed.error()) {
        std::cerr << "audio_qa_error: " << ayther::audio_qa::check_option_error_code(error->code);
        if (!error->option.empty())
            std::cerr << ": " << error->option;
        std::cerr << '\n';
        emit_user_message(ayther::audio_qa::CheckLanguage::spanish,
                          ayther::audio_qa::CheckMessage::invalid_options);
        return invalid_invocation_exit_code;
    }

    const auto language = parsed.options()->language == "en"
                              ? ayther::audio_qa::CheckLanguage::english
                              : ayther::audio_qa::CheckLanguage::spanish;

    const auto selection_result = ayther::audio_qa::select_check_takes(
        *parsed.options(), ayther::audio_qa::golden_axe_check_profile());
    const auto *selection = std::get_if<ayther::audio_qa::TakeSelection>(&selection_result);
    if (selection == nullptr) {
        std::cerr << "audio_qa_error: missing_profile_primary_take\n";
        emit_user_message(language, ayther::audio_qa::CheckMessage::missing_profile_primary_take);
        return invalid_invocation_exit_code;
    }

    const auto &options = *parsed.options();
    std::error_code directory_error;
    std::filesystem::create_directories(options.output, directory_error);
    if (directory_error || !std::filesystem::is_directory(options.output)) {
        std::cerr << "audio_qa_error: output_root_unavailable\n";
        emit_user_message(language, ayther::audio_qa::CheckMessage::output_root_unavailable);
        return evidence_failure_exit_code;
    }
    auto ledger_result = ayther::audio_qa::open_request_ledger(options.output);
    auto *ledger = std::get_if<ayther::audio_qa::RequestLedger>(&ledger_result);
    if (ledger == nullptr) {
        std::cerr << "audio_qa_error: request_ledger_unavailable\n";
        emit_user_message(language, ayther::audio_qa::CheckMessage::request_ledger_unavailable);
        return evidence_failure_exit_code;
    }
    ayther::audio_qa::SessionOccupancy occupancy;
    if (!ayther::audio_qa::restore_check_occupancy(*ledger, occupancy)) {
        std::cerr << "audio_qa_error: request_ledger_has_multiple_active_runs\n";
        emit_user_message(language, ayther::audio_qa::CheckMessage::request_ledger_inconsistent);
        return evidence_failure_exit_code;
    }

    const auto request_id = options.request_id.empty()
                                ? ayther::audio_qa::generate_check_id("request-")
                                : options.request_id;
    auto draft = ayther::audio_qa::make_check_request(options, *selection, request_id,
                                                      ayther::audio_qa::generate_check_id("run-"));
    const auto admitted_request = draft.request;
    const auto admission =
        ayther::audio_qa::admit_check_request(*ledger, occupancy, std::move(draft));
    if (std::holds_alternative<ayther::audio_qa::RequestLedgerError>(admission)) {
        std::cerr << "audio_qa_error: request_ledger_publish_failed\n";
        emit_user_message(language, ayther::audio_qa::CheckMessage::request_ledger_publish_failed);
        return evidence_failure_exit_code;
    }
    const auto &admitted = std::get<ayther::audio_qa::CheckAdmissionResult>(admission);
    switch (admitted.decision) {
    case ayther::audio_qa::CheckAdmissionDecision::accepted:
        std::cerr << "audio_qa_status: request_accepted: " << request_id << '\n';
        emit_user_message(language, ayther::audio_qa::CheckMessage::request_accepted);
        break;
    case ayther::audio_qa::CheckAdmissionDecision::known:
        std::cerr << "audio_qa_status: request_known: " << request_id << '\n';
        emit_user_message(language, ayther::audio_qa::CheckMessage::request_known);
        return incomplete_exit_code;
    case ayther::audio_qa::CheckAdmissionDecision::identity_conflict:
        std::cerr << "audio_qa_error: request_identity_conflict: " << request_id << '\n';
        emit_user_message(language, ayther::audio_qa::CheckMessage::request_identity_conflict);
        return invalid_invocation_exit_code;
    case ayther::audio_qa::CheckAdmissionDecision::busy:
        std::cerr << "audio_qa_error: session_busy: " << admitted.active_request_id << ':'
                  << admitted.active_run_id << '\n';
        emit_user_message(language, ayther::audio_qa::CheckMessage::session_busy);
        return invalid_invocation_exit_code;
    case ayther::audio_qa::CheckAdmissionDecision::invalid:
        std::cerr << "audio_qa_error: invalid_request\n";
        emit_user_message(language, ayther::audio_qa::CheckMessage::invalid_request);
        return invalid_invocation_exit_code;
    case ayther::audio_qa::CheckAdmissionDecision::capacity_exceeded:
        std::cerr << "audio_qa_error: request_capacity_exceeded\n";
        emit_user_message(language, ayther::audio_qa::CheckMessage::request_capacity_exceeded);
        return invalid_invocation_exit_code;
    }

    ayther::audio_qa::ControlledInterrupt interrupt;
    const ayther::audio_qa::ConsoleInterruptHandler interrupt_handler{interrupt};
    if (!interrupt_handler.installed()) {
        std::cerr << "audio_qa_error: interrupt_handler_unavailable\n";
        emit_user_message(language, ayther::audio_qa::CheckMessage::interrupt_handler_unavailable);
        return evidence_failure_exit_code;
    }

    std::vector<ayther::audio_qa::TakeTechnicalResult> results;
    results.reserve(selection->takes.size());
    bool all_replays_completed = true;
    std::uint64_t last_frame{};
    for (std::size_t index{}; index < selection->takes.size(); ++index) {
        auto request = admitted_request;
        request.take_ids = {selection->takes[index]};
        const std::string run_id =
            index == 0U ? admitted.run.run_id
                        : admitted.run.run_id + "-take-" + std::to_string(index + 1U);
        const auto executed = ayther::audio_qa::execute_check_replay(options, request, run_id);
        if (const auto *evidence =
                std::get_if<ayther::audio_qa::CheckExecutionEvidence>(&executed)) {
            const auto *preserved = evidence->preserved ? &*evidence->preserved : nullptr;
            const auto &replay = evidence->replay;
            if (replay.recording_frames > 0U)
                last_frame = replay.recording_frames - 1U;
            const bool complete =
                replay.succeeded && preserved != nullptr && preserved->relationships_reopened;
            all_replays_completed = all_replays_completed && complete;
            std::cerr << "audio_qa_replay: run_id=" << replay.run_id << " take=" << replay.take_id
                      << " recording_frames=" << replay.recording_frames
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
                std::cerr << " durable_facts=" << preserved->facts
                          << " durable_pcm_blocks=" << preserved->pcm_blocks
                          << " durable_pcm_bytes=" << preserved->pcm_bytes
                          << " fact_integrity_complete="
                          << (preserved->fact_integrity_complete ? "true" : "false")
                          << " relationships_reopened="
                          << (preserved->relationships_reopened ? "true" : "false")
                          << " runtime_data_isolated="
                          << (evidence->runtime_data_isolated ? "true" : "false");
            else if (evidence->preservation_error)
                std::cerr << " evidence_error="
                          << ayther::audio_qa::integrated_evidence_error_code(
                                 *evidence->preservation_error);
            std::cerr << " status=" << (complete ? "replay_evidence_reopened" : replay.code)
                      << '\n';
            results.push_back(
                {replay.take_id,
                 complete ? ayther::audio_qa::CheckTechnicalOutcome::complete
                          : ayther::audio_qa::CheckTechnicalOutcome::incomplete,
                 complete ? "replay_evidence_complete"
                          : (replay.succeeded ? "evidence_reopen_failed" : replay.code),
                 false, false});
        } else {
            all_replays_completed = false;
            const auto error = std::get<ayther::audio_qa::CheckExecutionError>(executed);
            const auto code = ayther::audio_qa::check_execution_error_code(error);
            std::cerr << "audio_qa_error: " << code << ": " << selection->takes[index] << '\n';
            results.push_back({selection->takes[index],
                               ayther::audio_qa::CheckTechnicalOutcome::incomplete,
                               std::string{code}, false, false});
        }
    }

    auto final_run = admitted.run;
    final_run.phase = ayther::audio_qa::Phase::closed;
    final_run.playback_result = all_replays_completed
                                    ? ayther::audio_qa::PlaybackResult::natural_end
                                    : ayther::audio_qa::PlaybackResult::error;
    final_run.evidence_result = all_replays_completed
                                    ? ayther::audio_qa::EvidenceResult::complete
                                    : ayther::audio_qa::EvidenceResult::incomplete;
    if (all_replays_completed)
        final_run.last_executed_frame = last_frame;
    final_run.cessation_confirmed = true;
    const auto ledger_update = ledger->update_run(final_run);
    const auto *updated = std::get_if<bool>(&ledger_update);
    if (updated == nullptr || !*updated) {
        std::cerr << "audio_qa_error: request_ledger_publish_failed\n";
        emit_user_message(language, ayther::audio_qa::CheckMessage::request_ledger_publish_failed);
        return evidence_failure_exit_code;
    }
    const auto summary = ayther::audio_qa::summarize_check_results(results);
    if (!summary) {
        std::cerr << "audio_qa_error: technical_summary_invalid\n";
        emit_user_message(language, ayther::audio_qa::CheckMessage::technical_summary_invalid);
        return evidence_failure_exit_code;
    }
    std::cerr << ayther::audio_qa::format_check_summary(*summary) << '\n';
    return summary->exit_code;
}
