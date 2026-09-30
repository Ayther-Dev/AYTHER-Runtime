#pragma once

#include "durable_file.h"
#include "pcm_block_store.h"

#include <cstddef>
#include <filesystem>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::size_t max_derived_wav_blocks = 4096;
inline constexpr std::size_t max_derived_wav_pcm_bytes = 64U * 1024U * 1024U;

struct DerivedWav {
    DurablePublishedFile publication;
    AudioFormat format;
    SampleFrameRange range;
    std::size_t block_count{};
    ContentIdentity pcm_identity;
};

enum class WavDerivationError {
    invalid_input,
    block_invalid,
    format_mismatch,
    non_contiguous,
    too_large,
    publish_failed,
};

using WavDerivationResult = std::variant<DerivedWav, WavDerivationError>;

[[nodiscard]] WavDerivationResult derive_wav(const std::vector<std::filesystem::path> &pcm_blocks,
                                             const std::filesystem::path &output_path) noexcept;

} // namespace ayther::audio_qa
