#pragma once

#include "recording_input_source.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ayther::audio_qa {

enum class RecordingStateError {
    none,
    invalid_layout,
    truncated_compressed_state,
    allocation_failed,
    decompression_failed,
};

struct GameStateRestoreOperationResult {
    bool succeeded{};
    std::string code;
    std::string detail;
};

using RestoreGameStateOperation =
    GameStateRestoreOperationResult (*)(void *context, const std::vector<std::uint8_t> &state);

struct RecordingReplayPreparationResult {
    RecordingStateError state_error{RecordingStateError::none};
    bool restore_attempted{};
    GameStateRestoreOperationResult restore{};
    std::optional<RecordingInputSource> inputs;
};

[[nodiscard]] RecordingReplayPreparationResult
prepare_recording_replay(std::span<const std::byte> recording, const RecordingLayout &layout,
                         void *restore_context, RestoreGameStateOperation restore) noexcept;

} // namespace ayther::audio_qa
