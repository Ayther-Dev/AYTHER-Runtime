#include "recording_input_source.h"

#include <limits>

namespace ayther::audio_qa {
namespace {

[[nodiscard]] bool contains(const std::span<const std::byte> bytes,
                            const RecordingByteRange range) noexcept {
    return range.offset <= bytes.size() && range.size <= bytes.size() - range.offset;
}

} // namespace

RecordingInputSourceError
initialize_recording_input_source(RecordingInputSource &source,
                                  const std::span<const std::byte> recording,
                                  const RecordingLayout &layout) noexcept {
    if (layout.frame_count < min_recording_frames || layout.frame_count > max_recording_frames) {
        return RecordingInputSourceError::invalid_frame_count;
    }
    const auto expected_bytes =
        static_cast<std::uint64_t>(layout.frame_count) * sizeof(std::uint16_t);
    if (layout.inputs.size != expected_bytes ||
        layout.inputs.offset > std::numeric_limits<std::size_t>::max()) {
        return RecordingInputSourceError::invalid_input_range;
    }
    if (!contains(recording, layout.inputs)) {
        return RecordingInputSourceError::truncated_input_range;
    }
    source.bytes_ = recording.subspan(static_cast<std::size_t>(layout.inputs.offset),
                                      static_cast<std::size_t>(layout.inputs.size));
    source.frame_count_ = layout.frame_count;
    return RecordingInputSourceError::none;
}

RecordingInputSourceResult make_recording_input_source(const std::span<const std::byte> recording,
                                                       const RecordingLayout &layout) noexcept {
    RecordingInputSourceResult result{};
    result.error = initialize_recording_input_source(result.source, recording, layout);
    return result;
}

std::optional<RecordingFrameInput> RecordingInputSource::next() noexcept {
    if (exhausted()) {
        return std::nullopt;
    }
    const auto offset = static_cast<std::size_t>(cursor_) * sizeof(std::uint16_t);
    const auto low = std::to_integer<std::uint16_t>(bytes_[offset]);
    const auto high = std::to_integer<std::uint16_t>(bytes_[offset + 1U]);
    const RecordingFrameInput input{cursor_, static_cast<std::uint16_t>(low | high << 8U)};
    ++cursor_;
    return input;
}

std::uint32_t RecordingInputSource::frame_count() const noexcept { return frame_count_; }

std::uint32_t RecordingInputSource::consumed() const noexcept { return cursor_; }

bool RecordingInputSource::exhausted() const noexcept { return cursor_ >= frame_count_; }

} // namespace ayther::audio_qa
