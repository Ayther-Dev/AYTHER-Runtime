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

RecordingStateError decompress_recording_state(const std::span<const std::byte> recording,
                                               const RecordingLayout &layout,
                                               std::vector<std::uint8_t> &state) {
    if (layout.raw_state_bytes == 0U || layout.raw_state_bytes > max_recording_state_bytes ||
        layout.compressed_state.size == 0U ||
        layout.compressed_state.offset > std::numeric_limits<std::size_t>::max() ||
        layout.compressed_state.size > std::numeric_limits<std::size_t>::max())
        return RecordingStateError::invalid_layout;
    if (!contains(recording, layout.compressed_state))
        return RecordingStateError::truncated_compressed_state;
    const auto compressed_offset = static_cast<std::size_t>(layout.compressed_state.offset);
    const auto compressed_size = static_cast<std::size_t>(layout.compressed_state.size);
    const auto *compressed = recording.data() + compressed_offset;
    // One frame, exactly the declared bytes, declaring (if at all) the raw size of the take.
    const auto frame_bytes = ZSTD_findFrameCompressedSize(compressed, compressed_size);
    if (ZSTD_isError(frame_bytes) != 0U || frame_bytes != compressed_size)
        return RecordingStateError::decompression_failed;
    const auto declared = ZSTD_getFrameContentSize(compressed, compressed_size);
    if (declared == ZSTD_CONTENTSIZE_ERROR ||
        (declared != ZSTD_CONTENTSIZE_UNKNOWN && declared != layout.raw_state_bytes))
        return RecordingStateError::decompression_failed;
    try {
        state.assign(layout.raw_state_bytes, 0U);
    } catch (const std::bad_alloc &) {
        return RecordingStateError::allocation_failed;
    }
    const auto decompressed =
        ZSTD_decompress(state.data(), state.size(), compressed, compressed_size);
    if (ZSTD_isError(decompressed) != 0U || decompressed != state.size())
        return RecordingStateError::decompression_failed;
    return RecordingStateError::none;
}

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
    result.state_error = decompress_recording_state(recording, layout, state);
    if (result.state_error != RecordingStateError::none)
        return result;

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
