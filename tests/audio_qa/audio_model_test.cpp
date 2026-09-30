#include "audio_chunk.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        require(qa::expected_payload_bytes({qa::PcmFormat::s16le, 44100, 2}, 3) == 12,
                "sample_frame_confused_with_channel_sample");
        require(qa::expected_payload_bytes({qa::PcmFormat::s24le, 48000, 2}, 3) == 18,
                "packed_24bit_width_wrong");
        const qa::AudioFormat maximum{qa::PcmFormat::f32le, 192000, 8};
        require(qa::expected_payload_bytes(maximum, qa::max_audio_payload_bytes / 32) ==
                    qa::max_audio_payload_bytes,
                "maximum_payload_boundary_rejected");
        require(!qa::expected_payload_bytes(maximum, qa::max_audio_payload_bytes / 32 + 1),
                "oversized_pcm_accepted");
        require(!qa::expected_payload_bytes(maximum, UINT64_MAX),
                "payload_multiplication_overflow");
        require(!qa::expected_payload_bytes({qa::PcmFormat::s16le, 192001, 2}, 1) &&
                    !qa::expected_payload_bytes({qa::PcmFormat::s16le, 44100, 9}, 1),
                "format_limits_ignored");

        qa::AudioChunk chunk;
        chunk.run_id = "run";
        chunk.capture_point = "session_postmix";
        chunk.producer_sequence = 1;
        chunk.format = {qa::PcmFormat::s16le, 44100, 2};
        chunk.range = {"output", 44100, 7, 10};
        chunk.bytes.resize(12);
        require(qa::well_formed(chunk), "valid_pending_pcm_rejected");
        chunk.bytes.pop_back();
        require(!qa::well_formed(chunk), "partial_sample_frame_accepted");
        chunk.bytes.push_back(std::byte{});
        chunk.durability = qa::Durability::confirmed;
        require(!qa::well_formed(chunk), "durable_without_checkpoint_accepted");
        chunk.sha256 = {qa::Availability::known, std::array<std::uint8_t, 32>{}, {}};
        chunk.checkpoint_id = {qa::Availability::known, "checkpoint-1", {}};
        require(qa::well_formed(chunk), "durability_metadata_rejected");
        qa::AudioDiscontinuity gap;
        gap.kind = qa::DiscontinuityKind::discarded_audio;
        gap.affected_range = {"mix", 44100, 10, 14};
        gap.output_range = {qa::Availability::not_applicable, std::nullopt,
                            "discarded_before_output"};
        chunk.discontinuities.assign(qa::max_audio_discontinuities, gap);
        require(qa::well_formed(chunk), "discard_without_output_or_gap_boundary_rejected");
        chunk.discontinuities.push_back(gap);
        require(!qa::well_formed(chunk), "discontinuity_limit_ignored");

        qa::Field<qa::TrackPosition> position{qa::Availability::not_applicable, std::nullopt,
                                              "original_audio_has_no_hd_track"};
        require(qa::consistent_availability(position), "not_applicable_position_rejected");
        const auto not_applicable = position;
        position = {qa::Availability::unknown, std::nullopt, "trace_lost"};
        require(qa::consistent_availability(position) && position != not_applicable,
                "unknown_position_confused_with_not_applicable");
        position.value = qa::TrackPosition{0, 44100};
        require(!qa::consistent_availability(position), "unknown_position_became_zero");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "qa_audio_model_test_failed: %s\n", error.what());
        return 1;
    }
}
