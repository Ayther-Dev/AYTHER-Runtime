#include "recording_replay_preparation.h"

#include <zstd.h>

#include <limits>
#include <new>

namespace ayther::audio_qa {
namespace {

[[nodiscard]] bool contains(const std::span<const std::byte> bytes,
                            const RecordingByteRange range) noexcept {
    return range.offset <= bytes.size() && range.size <= bytes.size() - range.offset;
}

} // namespace

RecordingReplayPreparationResult
prepare_recording_replay(const std::span<const std::byte> recording, const RecordingLayout &layout,
                         void *const restore_context,
                         const RestoreGameStateOperation restore) noexcept {
    RecordingReplayPreparationResult result{};
    if (layout.raw_state_bytes == 0U || layout.raw_state_bytes > max_recording_state_bytes ||
        layout.compressed_state.size == 0U ||
        layout.compressed_state.offset > std::numeric_limits<std::size_t>::max() ||
        layout.compressed_state.size > std::numeric_limits<std::size_t>::max()) {
        result.state_error = RecordingStateError::invalid_layout;
        return result;
    }
    if (!contains(recording, layout.compressed_state)) {
        result.state_error = RecordingStateError::truncated_compressed_state;
        return result;
    }
    const auto inputs = make_recording_input_source(recording, layout);
    if (inputs.error != RecordingInputSourceError::none) {
        result.state_error = RecordingStateError::invalid_layout;
        return result;
    }

    std::vector<std::uint8_t> state;
    try {
        state.resize(layout.raw_state_bytes);
    } catch (const std::bad_alloc &) {
        result.state_error = RecordingStateError::allocation_failed;
        return result;
    }

    const auto compressed_offset = static_cast<std::size_t>(layout.compressed_state.offset);
    const auto compressed_size = static_cast<std::size_t>(layout.compressed_state.size);
    const auto decompressed = ZSTD_decompress(
        state.data(), state.size(), recording.data() + compressed_offset, compressed_size);
    if (ZSTD_isError(decompressed) != 0U || decompressed != state.size()) {
        result.state_error = RecordingStateError::decompression_failed;
        return result;
    }

    if (restore == nullptr) {
        result.restore = {false, "restore_operation_missing",
                          "game state restore operation is unavailable"};
        return result;
    }
    result.restore_attempted = true;
    result.restore = restore(restore_context, state);
    if (!result.restore.succeeded) {
        return result;
    }

    result.inputs.emplace(inputs.source);
    return result;
}

} // namespace ayther::audio_qa
