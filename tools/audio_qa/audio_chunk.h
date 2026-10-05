#pragma once

#include "mix_model.h"

#include <array>
#include <cstddef>

namespace ayther::audio_qa {

enum class PcmFormat { s16le, s24le, s32le, f32le };
struct AudioFormat {
    PcmFormat pcm{PcmFormat::s16le};
    std::uint32_t sample_rate{};
    std::uint8_t channels{};
};
enum class Durability { pending, confirmed };
enum class DiscontinuityKind { inserted_silence, discarded_audio };
struct AudioDiscontinuity {
    DiscontinuityKind kind{DiscontinuityKind::inserted_silence};
    SampleFrameRange affected_range;
    Field<SampleFrameRange> output_range;
    std::vector<FactId> cause_ids;
};

struct AudioChunk {
    std::string run_id;
    std::string capture_point;
    std::uint64_t producer_sequence{};
    AudioFormat format;
    SampleFrameRange range;
    // Interleaved PCM bytes in the explicit byte order of format.pcm.
    std::vector<std::byte> bytes;
    Field<std::array<std::uint8_t, 32>> sha256;
    Durability durability{Durability::pending};
    Field<std::string> checkpoint_id;
    std::vector<FactId> cause_ids;
    std::vector<AudioDiscontinuity> discontinuities;
    // Spec 002, DI-14 (evidence 1.2): the linear segment of the traversal this PCM belongs to.
    // 0 from the start of the take; the Runtime opens the next one at every resume. A chunk
    // never spans two segments. Chunks of evidence 1.1 and earlier are all segment 0.
    std::uint64_t segment{};
};

[[nodiscard]] std::optional<std::size_t>
expected_payload_bytes(const AudioFormat &format, std::uint64_t sample_frames) noexcept;
// Does not compute a hash or confirm physical durability.
[[nodiscard]] bool well_formed(const AudioChunk &chunk) noexcept;

} // namespace ayther::audio_qa
