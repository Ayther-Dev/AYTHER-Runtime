#pragma once

#include "recording_header.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ayther::audio_qa {

struct RecordingFrameInput {
    std::uint32_t frame{};
    std::uint16_t buttons{};
};

enum class RecordingInputSourceError {
    none,
    invalid_frame_count,
    invalid_input_range,
    truncated_input_range,
};

class RecordingInputSource {
  public:
    [[nodiscard]] std::optional<RecordingFrameInput> next() noexcept;
    [[nodiscard]] std::uint32_t frame_count() const noexcept;
    [[nodiscard]] std::uint32_t consumed() const noexcept;
    [[nodiscard]] bool exhausted() const noexcept;

  private:
    friend struct RecordingInputSourceResult;
    friend RecordingInputSourceError
    initialize_recording_input_source(RecordingInputSource &source,
                                      std::span<const std::byte> recording,
                                      const RecordingLayout &layout) noexcept;

    std::span<const std::byte> bytes_{};
    std::uint32_t frame_count_{};
    std::uint32_t cursor_{};
};

struct RecordingInputSourceResult {
    RecordingInputSourceError error{RecordingInputSourceError::none};
    RecordingInputSource source{};
};

[[nodiscard]] RecordingInputSourceResult
make_recording_input_source(std::span<const std::byte> recording,
                            const RecordingLayout &layout) noexcept;

} // namespace ayther::audio_qa
