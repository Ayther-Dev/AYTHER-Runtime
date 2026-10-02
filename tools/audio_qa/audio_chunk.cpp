#include "audio_chunk.h"

#include <string_view>

namespace ayther::audio_qa {
namespace {
bool identifier(std::string_view value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}
} // namespace

std::optional<std::size_t> expected_payload_bytes(const AudioFormat &format,
                                                  std::uint64_t sample_frames) noexcept {
    if (format.sample_rate == 0 || format.sample_rate > 192000 || format.channels == 0 ||
        format.channels > 8) {
        return std::nullopt;
    }
    std::size_t width{};
    switch (format.pcm) {
    case PcmFormat::s16le:
        width = 2;
        break;
    case PcmFormat::s24le:
        width = 3;
        break;
    case PcmFormat::s32le:
    case PcmFormat::f32le:
        width = 4;
        break;
    default:
        return std::nullopt;
    }
    const std::size_t frame_bytes = width * format.channels;
    if (sample_frames > max_audio_payload_bytes / frame_bytes) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(sample_frames) * frame_bytes;
}

bool well_formed(const AudioChunk &chunk) noexcept {
    if (!identifier(chunk.run_id) || !identifier(chunk.capture_point) ||
        chunk.producer_sequence == 0 || !well_formed(chunk.range) ||
        chunk.range.sample_rate != chunk.format.sample_rate ||
        !consistent_availability(chunk.sha256) || !consistent_availability(chunk.checkpoint_id) ||
        chunk.cause_ids.size() > max_fact_causes ||
        chunk.discontinuities.size() > max_audio_discontinuities ||
        chunk.durability < Durability::pending || chunk.durability > Durability::confirmed) {
        return false;
    }
    const auto expected = expected_payload_bytes(chunk.format, chunk.range.end - chunk.range.begin);
    if (!expected || *expected != chunk.bytes.size() ||
        (chunk.checkpoint_id.value && !identifier(*chunk.checkpoint_id.value))) {
        return false;
    }
    if (chunk.durability == Durability::confirmed &&
        (!chunk.sha256.value || !chunk.checkpoint_id.value)) {
        return false;
    }
    for (std::size_t i{}; i < chunk.cause_ids.size(); ++i) {
        const auto &cause = chunk.cause_ids[i];
        if (cause.run_id != chunk.run_id || !identifier(cause.producer_id) ||
            cause.producer_sequence == 0)
            return false;
        for (std::size_t previous{}; previous < i; ++previous)
            if (chunk.cause_ids[previous] == cause)
                return false;
    }
    for (const auto &gap : chunk.discontinuities) {
        if (gap.kind < DiscontinuityKind::inserted_silence ||
            gap.kind > DiscontinuityKind::discarded_audio || !well_formed(gap.affected_range) ||
            !consistent_availability(gap.output_range) ||
            (gap.output_range.value && !well_formed(*gap.output_range.value)) ||
            gap.cause_ids.size() > max_fact_causes) {
            return false;
        }
        for (const auto &cause : gap.cause_ids) {
            if (cause.run_id != chunk.run_id || !identifier(cause.producer_id) ||
                cause.producer_sequence == 0) {
                return false;
            }
        }
    }
    return true;
}

} // namespace ayther::audio_qa
