#include "recording_replay_loop.h"

namespace ayther::audio_qa {

RecordingReplayLoop::RecordingReplayLoop(InitialStatePublication &publication,
                                         RecordingInputSource &inputs,
                                         ReplayProgressPublisher *const progress) noexcept
    : publication_(&publication), inputs_(&inputs), progress_(progress) {}

ReplayFrameResult
RecordingReplayLoop::execute_next(const ReplayFrameOperations &operations) noexcept {
    if (failed_) {
        return {ReplayFrameError::loop_failed,
                {},
                0,
                "loop_failed",
                "a previous frame operation failed"};
    }
    if (!publication_->input_permitted()) {
        return {ReplayFrameError::initial_state_not_acknowledged,
                {},
                0,
                "initial_state_not_acknowledged",
                {}};
    }
    if (operations.set_input == nullptr || operations.step == nullptr) {
        failed_ = true;
        return {
            ReplayFrameError::operations_unavailable, {}, 0, "frame_operations_unavailable", {}};
    }
    if (inputs_->consumed() != frames_completed_) {
        failed_ = true;
        return {ReplayFrameError::invalid_input_position, {}, 0, "input_position_mismatch", {}};
    }

    if (progress_ != nullptr && !progress_->started() && !progress_->publish_start()) {
        failed_ = true;
        return {ReplayFrameError::progress_failed, {}, 0, "replay_start_publication_failed", {}};
    }

    const auto input = publication_->next_input(*inputs_);
    if (!input.has_value()) {
        if (progress_ != nullptr && !progress_->finished() && !progress_->publish_finish()) {
            failed_ = true;
            return {
                ReplayFrameError::progress_failed, {}, 0, "replay_finish_publication_failed", {}};
        }
        return {ReplayFrameError::input_exhausted, {}, 0, "input_exhausted", {}};
    }
    if (input->frame != frames_completed_) {
        failed_ = true;
        return {ReplayFrameError::invalid_input_position, input, 0, "input_frame_mismatch", {}};
    }
    if (progress_ != nullptr && !progress_->publish_input(input->frame)) {
        failed_ = true;
        return {
            ReplayFrameError::progress_failed, input, 0, "input_progress_publication_failed", {}};
    }

    const auto set = operations.set_input(operations.context, input->frame, input->buttons);
    if (!set.succeeded) {
        failed_ = true;
        return {ReplayFrameError::set_input_failed, input, set.engine_frame, set.code, set.detail};
    }
    const auto stepped = operations.step(operations.context);
    if (!stepped.succeeded) {
        failed_ = true;
        return {ReplayFrameError::step_failed, input, stepped.engine_frame, stepped.code,
                stepped.detail};
    }

    ++frames_completed_;
    if (progress_ != nullptr && !progress_->publish_frame(input->frame, stepped.engine_frame)) {
        failed_ = true;
        return {ReplayFrameError::progress_failed,
                input,
                stepped.engine_frame,
                "frame_progress_publication_failed",
                {}};
    }
    return {ReplayFrameError::none, input, stepped.engine_frame, stepped.code, stepped.detail};
}

std::uint32_t RecordingReplayLoop::frames_completed() const noexcept { return frames_completed_; }

bool RecordingReplayLoop::failed() const noexcept { return failed_; }

} // namespace ayther::audio_qa
