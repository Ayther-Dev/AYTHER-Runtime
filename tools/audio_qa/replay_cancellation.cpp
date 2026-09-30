#include "replay_cancellation.h"

namespace ayther::audio_qa {
namespace {

bool valid_position(const std::uint32_t frames_completed,
                    const std::optional<std::uint64_t> engine_frame) noexcept {
    return frames_completed == 0 ? !engine_frame.has_value() : engine_frame.has_value();
}

} // namespace

bool ReplayCancellation::capture_request(
    const ReplayControlGate &controls, const std::uint32_t frames_completed,
    const std::optional<std::uint64_t> last_engine_frame) noexcept {
    if (!controls.cancellation_requested() || record_.requested ||
        !valid_position(frames_completed, last_engine_frame)) {
        return false;
    }
    record_.requested = true;
    record_.requested_after_frames = frames_completed;
    return true;
}

bool ReplayCancellation::apply_at_frame_boundary(
    const std::uint32_t frames_completed, const std::optional<std::uint64_t> last_engine_frame,
    RunLifecycle &lifecycle) noexcept {
    if (!record_.requested || record_.applied ||
        frames_completed < record_.requested_after_frames ||
        !valid_position(frames_completed, last_engine_frame) ||
        lifecycle.begin_closing(PlaybackResult::cancelled) != RunTransitionError::none) {
        return false;
    }

    record_.applied = true;
    record_.applied_after_frames = frames_completed;
    if (frames_completed != 0) {
        record_.last_executed_recording_frame = frames_completed - 1U;
    }
    record_.last_engine_frame = last_engine_frame;
    return true;
}

const ReplayCancellationRecord &ReplayCancellation::record() const noexcept { return record_; }

} // namespace ayther::audio_qa
