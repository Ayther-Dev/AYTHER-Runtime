#include "protocol_header.h"
#include "replay_execution_result.h"
#include <sstream>
#include <toml++/toml.hpp>

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
        const auto encoded = qa::encode_replay_execution_result(expected, 2U);
        const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
        require(message != nullptr, "valid_replay_result_not_encoded");
        const auto decoded = qa::decode_replay_execution_result(*message, 2U);
        const auto *actual = std::get_if<qa::ReplayExecutionResult>(&decoded);
        require(actual != nullptr && *actual == expected, "replay_result_round_trip_changed");
        const std::string_view payload{
            reinterpret_cast<const char *>(message->data() + qa::protocol_header_bytes),
            message->size() - qa::protocol_header_bytes};
        auto legacy_document = toml::parse(payload);
        legacy_document.erase("presentation");
        legacy_document.insert_or_assign("schema", "1.2");
        std::ostringstream legacy_text;
        legacy_text << legacy_document;
        const auto legacy_payload = legacy_text.str();
        const auto legacy_header = qa::encode_protocol_header(
            {qa::MessageType::terminal, static_cast<std::uint32_t>(legacy_payload.size()), 2U, 2U});
        std::vector<std::byte> legacy_bytes(legacy_header.begin(), legacy_header.end());
        for (const auto byte : legacy_payload)
            legacy_bytes.push_back(static_cast<std::byte>(byte));
        const auto legacy_decoded = qa::decode_replay_execution_result(legacy_bytes, 2U);
        const auto *legacy_result = std::get_if<qa::ReplayExecutionResult>(&legacy_decoded);
        require(legacy_result && *legacy_result == expected, "legacy_terminal_not_supported");
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
