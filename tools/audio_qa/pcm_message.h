#pragma once

#include "audio_chunk.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

enum class PcmMessageError {
    invalid_chunk,
    metadata_too_large,
    message_too_large,
    header_rejected,
    wrong_message_type,
    sequence_mismatch,
    truncated,
    length_mismatch,
    malformed_metadata,
    pcm_length_mismatch,
    hash_required,
    hash_mismatch,
};

using EncodedPcmMessage = std::variant<std::vector<std::byte>, PcmMessageError>;
using DecodedPcmMessage = std::variant<AudioChunk, PcmMessageError>;

[[nodiscard]] std::array<std::uint8_t, 32> pcm_sha256(std::span<const std::byte> bytes) noexcept;

[[nodiscard]] EncodedPcmMessage encode_pcm_message(const AudioChunk &chunk,
                                                   std::uint64_t channel_sequence);
[[nodiscard]] DecodedPcmMessage decode_pcm_message(std::span<const std::byte> message,
                                                   std::uint64_t expected_sequence);

} // namespace ayther::audio_qa
