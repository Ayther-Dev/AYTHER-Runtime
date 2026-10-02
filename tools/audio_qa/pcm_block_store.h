#pragma once

#include "audio_chunk.h"
#include "exclusive_evidence_directory.h"
#include "reference_model.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <variant>

namespace ayther::audio_qa {

inline constexpr std::size_t pcm_block_header_bytes = 64;

struct StoredPcmBlock {
    std::filesystem::path path;
    std::uint64_t sequence{};
    ContentIdentity message_identity;
    ContentIdentity document_identity;
    AudioChunk chunk;
};

enum class PcmBlockStoreError {
    invalid_sequence,
    invalid_chunk,
    document_too_large,
    already_exists,
    io_error,
    malformed_header,
    incompatible_version,
    length_mismatch,
    sequence_mismatch,
    hash_mismatch,
    pcm_length_mismatch,
    invalid_content,
};

using PcmBlockStoreResult = std::variant<StoredPcmBlock, PcmBlockStoreError>;

[[nodiscard]] PcmBlockStoreResult write_pcm_block(const ExclusiveEvidenceDirectory &directory,
                                                  std::uint64_t sequence,
                                                  const AudioChunk &chunk) noexcept;

[[nodiscard]] PcmBlockStoreResult read_pcm_block(const std::filesystem::path &path) noexcept;

} // namespace ayther::audio_qa
