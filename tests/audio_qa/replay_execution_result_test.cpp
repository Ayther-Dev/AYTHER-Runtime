#include "protocol_header.h"
#include "replay_execution_result.h"
#include <sstream>
#include <toml++/toml.hpp>

#include <cstddef>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

void require(const bool condition, const char *const message) {
    if (!condition)
        throw std::runtime_error(message);
}

qa::ReplayExecutionResult result() {
    qa::ReplayExecutionResult value;
    value.run_id = "run-qa170";
    value.take_id = "public-synthetic.arp";
    value.recording_frames = 6;
    value.inputs_consumed = 6;
    value.assignment_count = 1;
    value.initial_game_state.byte_size = 16;
    value.initial_game_state.sha256[0] = 0x12;
    value.final_game_state.byte_size = 16;
    value.final_game_state.sha256[0] = 0x34;
    value.trace.observed_fact_count = 23;
    value.trace.ingress = {3, 2};
    value.trace.candidate = {5, 4};
    value.trace.selection = {5, 6};
    value.trace.playback_request = {5, 7};
    value.trace.playback_decision = {5, 8};
    value.trace.playback_effect = {5, 9};
    value.trace.mix_span = {6, 1};
    value.trace.occurrence = 1;
    value.trace.causally_connected = true;
    value.trace.loss_free = true;
    value.succeeded = true;
    value.code = "replay_complete";
    qa::describe_linear_terminal(value, false);
    return value;
}

// The terminal without the fields of a newer schema, as an older Runtime wrote it.
std::vector<std::byte> downgraded(const std::vector<std::byte> &message, std::string_view schema,
                                  bool without_presentation) {
    const std::string_view payload{
        reinterpret_cast<const char *>(message.data() + qa::protocol_header_bytes),
        message.size() - qa::protocol_header_bytes};
    auto document = toml::parse(payload);
    for (const auto *key : {"traversal", "playback", "linear_completed", "evidence_reasons",
                            "ended_paused", "user_pause_ms", "interruptions"})
        document.erase(key);
    if (without_presentation)
        document.erase("presentation");
    document.insert_or_assign("schema", std::string{schema});
    std::ostringstream text;
    text << document;
    const auto bytes = text.str();
    const auto header = qa::encode_protocol_header(
        {qa::MessageType::terminal, static_cast<std::uint32_t>(bytes.size()), 2U, 2U});
    std::vector<std::byte> result(header.begin(), header.end());
    for (const auto byte : bytes)
        result.push_back(static_cast<std::byte>(byte));
    return result;
}

qa::ReplayExecutionResult without_terminal_fields(qa::ReplayExecutionResult value) {
    value.traversal.reset();
    value.playback.reset();
    value.linear_completed = false;
    value.evidence_reasons.clear();
    value.ended_paused = false;
    value.user_pause_ms = 0U;
    value.interruptions = 0U;
    return value;
}

} // namespace

int main() {
    try {
        const auto expected = result();
        auto visible = expected;
        visible.presentation.mode = "visible";
        visible.presentation.code = "presented";
        visible.presentation.output_profile = "lcd";
        visible.presentation.audio_backend = "wasapi";
        visible.presentation.presented_frames = 6U;
        const auto visible_encoded = qa::encode_replay_execution_result(visible, 2U);
        const auto *visible_bytes = std::get_if<std::vector<std::byte>>(&visible_encoded);
        require(visible_bytes != nullptr, "visible_result_not_encoded");
        const auto visible_decoded = qa::decode_replay_execution_result(*visible_bytes, 2U);
        const auto *visible_result = std::get_if<qa::ReplayExecutionResult>(&visible_decoded);
        require(visible_result && *visible_result == visible, "visible_result_not_preserved");
        visible.presentation.affect(2U);
        visible.presentation.affect(2U);
        visible.presentation.affect(4U);
        require(visible.presentation.affected_frames == 2U &&
                    visible.presentation.first_affected_frame == 2U &&
                    visible.presentation.last_affected_frame == 4U && !qa::well_formed(visible),
                "degraded_presentation_claimed_complete");
        visible.succeeded = false;
        visible.code = "presentation_incomplete";
        require(qa::well_formed(visible), "incomplete_presentation_rejected");
        visible.presentation.last_affected_frame = 6U;
        require(!qa::well_formed(visible), "out_of_bounds_presentation_accepted");

        // D-8 (campaign 2026-10-04): after stepping back, frames are presented again and may
        // be affected again, in another order. Each take frame counts once and the range spans
        // all of them, so the terminal stays well formed and can be sent.
        auto revisited = expected;
        revisited.presentation.mode = "visible";
        revisited.presentation.code = "presented";
        for (std::uint32_t frame = 0; frame < 6U; ++frame)
            revisited.presentation.presented(frame);
        revisited.presentation.presented(3U);
        revisited.presentation.presented(4U);
        require(revisited.presentation.presented_frames == 6U && qa::well_formed(revisited),
                "revisited_frames_counted_twice");
        revisited.presentation.affect(4U);
        revisited.presentation.affect(3U);
        revisited.presentation.affect(4U);
        revisited.succeeded = false;
        revisited.code = "presentation_incomplete";
        require(revisited.presentation.affected_frames == 2U &&
                    revisited.presentation.first_affected_frame == 3U &&
                    revisited.presentation.last_affected_frame == 4U &&
                    qa::well_formed(revisited) &&
                    std::holds_alternative<std::vector<std::byte>>(
                        qa::encode_replay_execution_result(revisited, 2U)),
                "revisited_affected_frames_malformed");
        // D-14 (campaign 2026-10-05, BR-191; RF-2.6, plan §5.10 step 3): an interruption of
        // the presentation, even recovered and with every frame presented, leaves the
        // audiovisual observation incomplete, with the interruption as its reason. The
        // playback is not touched: the take still ends naturally.
        for (const char *cause : {"window_minimized", "audio_device_removed"}) {
            auto recovered = expected;
            recovered.presentation.mode = "visible";
            recovered.presentation.code = "presented";
            for (std::uint32_t frame = 0; frame < 6U; ++frame)
                recovered.presentation.presented(frame);
            recovered.presentation.interrupted(cause);
            recovered.presentation.interrupted("video_acquire_failed");
            require(!recovered.presentation.complete() &&
                        recovered.presentation.code == std::string_view{cause} &&
                        recovered.presentation.affected_frames == 0U &&
                        recovered.presentation.presented_frames == 6U,
                    "D-14: a recovered interruption is an incomplete audiovisual observation, "
                    "named by its first cause");
            require(!qa::well_formed(recovered),
                    "D-14: a terminal with an interrupted presentation cannot claim success");
            recovered.succeeded = false;
            recovered.code = "presentation_incomplete";
            qa::describe_linear_terminal(recovered, false);
            require(recovered.playback == std::optional<std::string>{"natural_end"} &&
                        recovered.linear_completed &&
                        recovered.evidence_reasons ==
                            std::vector<std::string>{"presentation_incomplete"} &&
                        qa::well_formed(recovered) &&
                        std::holds_alternative<std::vector<std::byte>>(
                            qa::encode_replay_execution_result(recovered, 2U)),
                    "D-14: the take ends naturally with presentation_incomplete");
        }
        auto degraded_first = expected;
        degraded_first.presentation.mode = "visible";
        degraded_first.presentation.code = "video_acquire_failed";
        degraded_first.presentation.interrupted("window_minimized");
        require(degraded_first.presentation.code == "video_acquire_failed",
                "D-14: the first problem of the presentation keeps naming it");

        const auto encoded = qa::encode_replay_execution_result(expected, 2U);
        const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
        require(message != nullptr, "valid_replay_result_not_encoded");
        const auto decoded = qa::decode_replay_execution_result(*message, 2U);
        const auto *actual = std::get_if<qa::ReplayExecutionResult>(&decoded);
        require(actual != nullptr && *actual == expected, "replay_result_round_trip_changed");
        // Spec 002 (contracts.md C1-5, C2): terminal 1.4 keeps playback, traversal and
        // evidence apart; 1.3 and 1.2 are still read, with those fields unknown.
        const std::string_view payload{
            reinterpret_cast<const char *>(message->data() + qa::protocol_header_bytes),
            message->size() - qa::protocol_header_bytes};
        const auto document = toml::parse(payload);
        require(document["schema"].value<std::string>() == "1.4" &&
                    document["traversal"].value<std::string>() == "linear" &&
                    document["playback"].value<std::string>() == "natural_end" &&
                    document["linear_completed"].value<bool>() == true &&
                    document["user_pause_ms"].value<std::string>() == "0",
                "terminal_1_4_fields_missing");
        const auto previous =
            qa::decode_replay_execution_result(downgraded(*message, "1.3", false), 2U);
        const auto *previous_result = std::get_if<qa::ReplayExecutionResult>(&previous);
        require(previous_result && *previous_result == without_terminal_fields(expected) &&
                    !previous_result->traversal && !previous_result->playback,
                "terminal_1_3_not_read_with_unknown_traversal");
        const auto legacy_decoded =
            qa::decode_replay_execution_result(downgraded(*message, "1.2", true), 2U);
        const auto *legacy_result = std::get_if<qa::ReplayExecutionResult>(&legacy_decoded);
        require(legacy_result && *legacy_result == without_terminal_fields(expected),
                "legacy_terminal_not_supported");
        require(std::holds_alternative<qa::ReplayExecutionResultError>(
                    qa::encode_replay_execution_result(without_terminal_fields(expected), 2U)),
                "terminal_without_playback_was_encoded");
        auto cancelled = expected;
        cancelled.succeeded = false;
        cancelled.code = "replay_cancelled";
        cancelled.inputs_consumed = 3;
        qa::describe_linear_terminal(cancelled, true);
        require(cancelled.playback == "cancelled" && !cancelled.linear_completed &&
                    cancelled.evidence_reasons.empty(),
                "cancelled_terminal_not_described");
        auto cancelled_early = expected;
        cancelled_early.succeeded = false;
        cancelled_early.code = "replay_cancelled";
        cancelled_early.inputs_consumed = 0;
        cancelled_early.evidence_reasons = {"audio_trace_incomplete_empty_pcm"};
        qa::describe_linear_terminal(cancelled_early, true);
        require(cancelled_early.playback == "cancelled" &&
                    cancelled_early.evidence_reasons ==
                        std::vector<std::string>{"audio_trace_incomplete_empty_pcm"},
                "cancellation_lost_its_evidence_reasons");
        auto failed = expected;
        failed.succeeded = false;
        failed.code = "game_state_restore_failed";
        failed.inputs_consumed = 0;
        qa::describe_linear_terminal(failed, false);
        require(failed.playback == "failed" &&
                    failed.evidence_reasons ==
                        std::vector<std::string>{"game_state_restore_failed"},
                "failed_terminal_not_described");
        const auto wrong_sequence = qa::decode_replay_execution_result(*message, 3U);
        require(std::get_if<qa::ReplayExecutionResultError>(&wrong_sequence) != nullptr,
                "sequence_mismatch_was_accepted");
        auto truncated = *message;
        truncated.pop_back();
        const auto truncated_result = qa::decode_replay_execution_result(truncated, 2U);
        require(std::get_if<qa::ReplayExecutionResultError>(&truncated_result) != nullptr,
                "truncated_result_was_accepted");
        auto incomplete_trace = expected;
        incomplete_trace.trace.causally_connected = false;
        const auto rejected = qa::encode_replay_execution_result(incomplete_trace, 2U);
        require(std::get_if<qa::ReplayExecutionResultError>(&rejected) != nullptr,
                "successful_result_with_incomplete_trace_was_encoded");
        auto streamed = expected;
        streamed.code = "replay_evidence_streamed";
        streamed.trace.ingress = {};
        streamed.trace.candidate = {};
        streamed.trace.selection = {};
        streamed.trace.playback_request = {};
        streamed.trace.playback_decision = {};
        streamed.trace.playback_effect = {};
        streamed.trace.mix_span = {};
        streamed.trace.occurrence = 0U;
        streamed.trace.causally_connected = false;
        const auto streamed_encoded = qa::encode_replay_execution_result(streamed, 2U);
        const auto *streamed_message = std::get_if<std::vector<std::byte>>(&streamed_encoded);
        require(streamed_message != nullptr,
                "streamed_result_without_runtime_causal_summary_was_rejected");
        const auto streamed_decoded = qa::decode_replay_execution_result(*streamed_message, 2U);
        const auto *streamed_actual = std::get_if<qa::ReplayExecutionResult>(&streamed_decoded);
        require(streamed_actual != nullptr && *streamed_actual == streamed,
                "streamed_result_round_trip_changed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "replay_execution_result_test: %s\n", error.what());
        return 1;
    }
}
