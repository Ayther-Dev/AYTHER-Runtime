#pragma once

#include "initial_state_publication.h"
#include "replay_progress.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace ayther::audio_qa {

struct ReplayFrameOperationResult {
    bool succeeded{};
    std::uint64_t engine_frame{};
    std::string_view code;
    std::string_view detail;
};

using SetRecordedInputOperation = ReplayFrameOperationResult (*)(void *context,
                                                                 std::uint32_t recording_frame,
                                                                 std::uint16_t buttons) noexcept;
using StepRecordedFrameOperation = ReplayFrameOperationResult (*)(void *context) noexcept;

struct ReplayFrameOperations {
    void *context{};
    SetRecordedInputOperation set_input{};
    StepRecordedFrameOperation step{};
};

enum class ReplayFrameError {
    none,
    initial_state_not_acknowledged,
    operations_unavailable,
    invalid_input_position,
    input_exhausted,
    set_input_failed,
    step_failed,
    loop_failed,
    progress_failed,
};

struct ReplayFrameResult {
    ReplayFrameError error{ReplayFrameError::none};
    std::optional<RecordingFrameInput> input;
    std::uint64_t engine_frame{};
    std::string_view operation_code;
    std::string_view operation_detail;
};

class RecordingReplayLoop final {
  public:
    RecordingReplayLoop(InitialStatePublication &publication, RecordingInputSource &inputs,
                        ReplayProgressPublisher *progress = nullptr) noexcept;

    [[nodiscard]] ReplayFrameResult execute_next(const ReplayFrameOperations &operations) noexcept;

    [[nodiscard]] std::uint32_t frames_completed() const noexcept;
    [[nodiscard]] bool failed() const noexcept;

  private:
    InitialStatePublication *publication_{};
    RecordingInputSource *inputs_{};
    ReplayProgressPublisher *progress_{};
    std::uint32_t frames_completed_{};
    bool failed_{};
};

} // namespace ayther::audio_qa
