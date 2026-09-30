#include "pcm_message.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

template <typename UInt>
void write_le(std::vector<std::byte> &bytes, const std::size_t offset, const UInt value) {
    for (std::size_t index = 0; index < sizeof(UInt); ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

void require(const bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_error(const qa::DecodedPcmMessage &result, const qa::PcmMessageError expected,
                  const char *message) {
    require(std::holds_alternative<qa::PcmMessageError>(result) &&
                std::get<qa::PcmMessageError>(result) == expected,
            message);
}

qa::AudioChunk sample_chunk(const qa::PcmFormat format, const std::uint32_t rate,
                            const std::uint8_t channels, const std::uint64_t sequence) {
    qa::AudioChunk chunk;
    chunk.run_id = "run-93";
    chunk.capture_point = "session-postmix";
    chunk.producer_sequence = sequence;
    chunk.format = {format, rate, channels};
    chunk.range = {"engine-main-output", rate, 17, 20};
    const auto size = qa::expected_payload_bytes(chunk.format, 3);
    require(size.has_value(), "sample_format_rejected");
    chunk.bytes.resize(*size);
    for (std::size_t index = 0; index < chunk.bytes.size(); ++index) {
        chunk.bytes[index] = static_cast<std::byte>((index * 29U + sequence) & 0xffU);
    }
    chunk.sha256 = {qa::Availability::known, qa::pcm_sha256(chunk.bytes), {}};
    return chunk;
}

bool same_discontinuity(const qa::AudioDiscontinuity &left, const qa::AudioDiscontinuity &right) {
    return left.kind == right.kind && left.affected_range == right.affected_range &&
           left.output_range == right.output_range && left.cause_ids == right.cause_ids;
}

void require_same(const qa::AudioChunk &expected, const qa::AudioChunk &actual) {
    require(expected.run_id == actual.run_id && expected.capture_point == actual.capture_point &&
                expected.producer_sequence == actual.producer_sequence &&
                expected.format.pcm == actual.format.pcm &&
                expected.format.sample_rate == actual.format.sample_rate &&
                expected.format.channels == actual.format.channels &&
                expected.range == actual.range && expected.bytes == actual.bytes &&
                expected.sha256 == actual.sha256 && expected.durability == actual.durability &&
                expected.checkpoint_id == actual.checkpoint_id &&
                expected.cause_ids == actual.cause_ids &&
                expected.discontinuities.size() == actual.discontinuities.size(),
            "pcm_round_trip_changed_chunk");
    for (std::size_t index = 0; index < expected.discontinuities.size(); ++index) {
        require(same_discontinuity(expected.discontinuities[index], actual.discontinuities[index]),
                "pcm_round_trip_changed_discontinuity");
    }
}

} // namespace

int main() {
    try {
        const std::array<std::byte, 3> abc{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
        constexpr std::array<std::uint8_t, 32> abc_sha{
            0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
            0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
            0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
        require(qa::pcm_sha256(abc) == abc_sha, "sha256_known_vector_failed");

        const std::array formats{std::pair{qa::PcmFormat::s16le, std::uint32_t{44100}},
                                 std::pair{qa::PcmFormat::s24le, std::uint32_t{48000}},
                                 std::pair{qa::PcmFormat::s32le, std::uint32_t{96000}},
                                 std::pair{qa::PcmFormat::f32le, std::uint32_t{192000}}};
        std::vector<std::byte> baseline;
        for (std::size_t index = 0; index < formats.size(); ++index) {
            auto chunk = sample_chunk(formats[index].first, formats[index].second,
                                      static_cast<std::uint8_t>(index + 1), index + 1);
            if (index == 0) {
                chunk.cause_ids = {{"run-93", "audio-mixer", 7}};
                qa::AudioDiscontinuity discontinuity;
                discontinuity.kind = qa::DiscontinuityKind::inserted_silence;
                discontinuity.affected_range = {"engine-main-output", 44100, 18, 19};
                discontinuity.output_range = {
                    qa::Availability::known,
                    qa::SampleFrameRange{"captured-output", 44100, 101, 102},
                    {}};
                discontinuity.cause_ids = {{"run-93", "audio-mixer", 8}};
                chunk.discontinuities.push_back(discontinuity);
            }
            const auto encoded = qa::encode_pcm_message(chunk, index + 30);
            const auto *bytes = std::get_if<std::vector<std::byte>>(&encoded);
            require(bytes != nullptr, "pcm_format_encoding_failed");
            const auto decoded = qa::decode_pcm_message(*bytes, index + 30);
            const auto *round_trip = std::get_if<qa::AudioChunk>(&decoded);
            require(round_trip != nullptr, "pcm_format_decoding_failed");
            require_same(chunk, *round_trip);
            if (index == 0) {
                baseline = *bytes;
            }
        }

        auto inconsistent = baseline;
        const auto original_pcm_bytes = static_cast<std::uint32_t>(
            sample_chunk(qa::PcmFormat::s16le, 44100, 1, 1).bytes.size());
        inconsistent.pop_back();
        write_le<std::uint32_t>(inconsistent, 16,
                                static_cast<std::uint32_t>(inconsistent.size() - 40U));
        write_le<std::uint32_t>(inconsistent, 44, original_pcm_bytes - 1U);
        expect_error(qa::decode_pcm_message(inconsistent, 30),
                     qa::PcmMessageError::pcm_length_mismatch, "inconsistent_pcm_length_accepted");

        auto corrupt_pcm = baseline;
        corrupt_pcm.back() ^= std::byte{0x01};
        expect_error(qa::decode_pcm_message(corrupt_pcm, 30), qa::PcmMessageError::hash_mismatch,
                     "pcm_hash_mismatch_accepted");
        expect_error(qa::decode_pcm_message(baseline, 31), qa::PcmMessageError::sequence_mismatch,
                     "pcm_sequence_mismatch_accepted");
        expect_error(qa::decode_pcm_message(std::span<const std::byte>{baseline}.first(39), 30),
                     qa::PcmMessageError::header_rejected, "truncated_pcm_header_accepted");

        auto missing_hash = sample_chunk(qa::PcmFormat::s16le, 44100, 1, 7);
        missing_hash.sha256 = {};
        const auto missing_result = qa::encode_pcm_message(missing_hash, 41);
        require(std::holds_alternative<qa::PcmMessageError>(missing_result) &&
                    std::get<qa::PcmMessageError>(missing_result) ==
                        qa::PcmMessageError::hash_required,
                "pcm_without_hash_encoded");

        qa::AudioChunk oversized;
        oversized.run_id = "run-93";
        oversized.capture_point = "session-postmix";
        oversized.producer_sequence = 8;
        oversized.format = {qa::PcmFormat::s16le, 44100, 1};
        oversized.range = {"engine-main-output", 44100, 0, qa::max_audio_payload_bytes / 2};
        oversized.bytes.resize(qa::max_audio_payload_bytes);
        oversized.sha256 = {qa::Availability::known, qa::pcm_sha256(oversized.bytes), {}};
        const auto oversized_result = qa::encode_pcm_message(oversized, 42);
        require(std::holds_alternative<qa::PcmMessageError>(oversized_result) &&
                    std::get<qa::PcmMessageError>(oversized_result) ==
                        qa::PcmMessageError::message_too_large,
                "pcm_message_limit_ignored");

        std::puts("pcm_message_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "pcm_message_test: %s\n", error.what());
        return 1;
    }
}
