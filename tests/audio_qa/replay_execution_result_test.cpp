#include "replay_execution_result.h"

#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <variant>

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
    return value;
}

} // namespace

int main() {
    try {
        const auto expected = result();
        const auto encoded = qa::encode_replay_execution_result(expected, 2U);
        const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
        require(message != nullptr, "valid_replay_result_not_encoded");
        const auto decoded = qa::decode_replay_execution_result(*message, 2U);
        const auto *actual = std::get_if<qa::ReplayExecutionResult>(&decoded);
        require(actual != nullptr && *actual == expected, "replay_result_round_trip_changed");
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
        const auto *streamed_message =
            std::get_if<std::vector<std::byte>>(&streamed_encoded);
        require(streamed_message != nullptr,
                "streamed_result_without_runtime_causal_summary_was_rejected");
        const auto streamed_decoded =
            qa::decode_replay_execution_result(*streamed_message, 2U);
        const auto *streamed_actual =
            std::get_if<qa::ReplayExecutionResult>(&streamed_decoded);
        require(streamed_actual != nullptr && *streamed_actual == streamed,
                "streamed_result_round_trip_changed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "replay_execution_result_test: %s\n", error.what());
        return 1;
    }
}
