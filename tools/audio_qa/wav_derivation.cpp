#include "wav_derivation.h"

#include "content_hash.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace ayther::audio_qa {
namespace {

constexpr std::size_t wav_header_bytes = 44;

struct WavEncoding {
    std::uint16_t format_tag{};
    std::uint16_t bits_per_sample{};
};

WavEncoding encoding(const PcmFormat format) {
    switch (format) {
    case PcmFormat::s16le:
        return {1, 16};
    case PcmFormat::s24le:
        return {1, 24};
    case PcmFormat::s32le:
        return {1, 32};
    case PcmFormat::f32le:
        return {3, 32};
    }
    return {};
}

void put_u16(std::vector<std::byte> &bytes, const std::size_t offset, const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void put_u32(std::vector<std::byte> &bytes, const std::size_t offset, const std::uint32_t value) {
    for (std::size_t index{}; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

void put_text(std::vector<std::byte> &bytes, const std::size_t offset,
              const std::array<char, 4> text) {
    for (std::size_t index{}; index < text.size(); ++index) {
        bytes[offset + index] = static_cast<std::byte>(text[index]);
    }
}

bool same_format(const AudioFormat &left, const AudioFormat &right) noexcept {
    return left.pcm == right.pcm && left.sample_rate == right.sample_rate &&
           left.channels == right.channels;
}

} // namespace

WavDerivationResult derive_wav(const std::vector<std::filesystem::path> &pcm_blocks,
                               const std::filesystem::path &output_path) noexcept {
    try {
        if (pcm_blocks.empty() || pcm_blocks.size() > max_derived_wav_blocks ||
            output_path.empty() || output_path.filename().empty()) {
            return WavDerivationError::invalid_input;
        }
        std::vector<StoredPcmBlock> blocks;
        blocks.reserve(pcm_blocks.size());
        std::size_t pcm_size{};
        for (const auto &path : pcm_blocks) {
            const auto read = read_pcm_block(path);
            const auto *block = std::get_if<StoredPcmBlock>(&read);
            if (block == nullptr) {
                return WavDerivationError::block_invalid;
            }
            if (!blocks.empty()) {
                const auto &previous = blocks.back().chunk;
                if (!same_format(previous.format, block->chunk.format) ||
                    previous.run_id != block->chunk.run_id ||
                    previous.capture_point != block->chunk.capture_point ||
                    previous.range.timeline_id != block->chunk.range.timeline_id ||
                    previous.range.sample_rate != block->chunk.range.sample_rate) {
                    return WavDerivationError::format_mismatch;
                }
                if (previous.range.end != block->chunk.range.begin) {
                    return WavDerivationError::non_contiguous;
                }
            }
            if (block->chunk.bytes.size() > max_derived_wav_pcm_bytes - pcm_size) {
                return WavDerivationError::too_large;
            }
            pcm_size += block->chunk.bytes.size();
            blocks.push_back(*block);
        }
        if (pcm_size > (std::numeric_limits<std::uint32_t>::max)() - 36U) {
            return WavDerivationError::too_large;
        }
        const auto wav_encoding = encoding(blocks.front().chunk.format.pcm);
        const auto channels = blocks.front().chunk.format.channels;
        const auto block_align =
            static_cast<std::uint16_t>(channels * (wav_encoding.bits_per_sample / 8U));
        const auto byte_rate = blocks.front().chunk.format.sample_rate * block_align;
        std::vector<std::byte> wav(wav_header_bytes + pcm_size);
        put_text(wav, 0, {'R', 'I', 'F', 'F'});
        put_u32(wav, 4, static_cast<std::uint32_t>(36U + pcm_size));
        put_text(wav, 8, {'W', 'A', 'V', 'E'});
        put_text(wav, 12, {'f', 'm', 't', ' '});
        put_u32(wav, 16, 16);
        put_u16(wav, 20, wav_encoding.format_tag);
        put_u16(wav, 22, channels);
        put_u32(wav, 24, blocks.front().chunk.format.sample_rate);
        put_u32(wav, 28, byte_rate);
        put_u16(wav, 32, block_align);
        put_u16(wav, 34, wav_encoding.bits_per_sample);
        put_text(wav, 36, {'d', 'a', 't', 'a'});
        put_u32(wav, 40, static_cast<std::uint32_t>(pcm_size));
        std::size_t offset = wav_header_bytes;
        ContentHasher pcm_hasher;
        for (const auto &block : blocks) {
            const auto pcm = std::span<const std::byte>{block.chunk.bytes};
            std::copy(pcm.begin(), pcm.end(), wav.begin() + offset);
            pcm_hasher.update(pcm);
            offset += pcm.size();
        }
        const auto published = publish_durable_file(output_path, wav);
        const auto *file = std::get_if<DurablePublishedFile>(&published);
        if (file == nullptr) {
            return WavDerivationError::publish_failed;
        }
        auto range = blocks.front().chunk.range;
        range.end = blocks.back().chunk.range.end;
        return DerivedWav{*file, blocks.front().chunk.format, std::move(range), blocks.size(),
                          pcm_hasher.finish()};
    } catch (...) {
        return WavDerivationError::publish_failed;
    }
}

} // namespace ayther::audio_qa
