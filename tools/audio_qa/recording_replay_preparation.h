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

// Spec 002 (RF-2.2, D-2 of the 2026-10-04 campaign): the initial state of the take, whole.
// The compressed state must be one zstd frame spanning exactly its declared bytes, the size
// that frame declares (when it declares one) must be the raw size of the take, and it must
// decompress to exactly that size. The supervisor runs it before admission and the Runtime
// before restoring, so both judge the same bytes the same way.
[[nodiscard]] RecordingStateError decompress_recording_state(std::span<const std::byte> recording,
                                                             const RecordingLayout &layout,
                                                             std::vector<std::uint8_t> &state);

[[nodiscard]] RecordingReplayPreparationResult
prepare_recording_replay(std::span<const std::byte> recording, const RecordingLayout &layout,
                         void *restore_context, RestoreGameStateOperation restore) noexcept;

} // namespace ayther::audio_qa
