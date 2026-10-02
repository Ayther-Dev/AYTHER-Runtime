#include "replay_progress.h"

namespace ayther::audio_qa {

ReplayProgressPublisher::ReplayProgressPublisher(const std::uint32_t total_inputs,
                                                 void *const context,
                                                 const ReplayProgressObserver observer) noexcept
    : total_inputs_(total_inputs), context_(context), observer_(observer) {}

bool ReplayProgressPublisher::publish_acceptance() noexcept {
    if (observer_ == nullptr || total_inputs_ == 0 || state_ != State::pending) {
        return false;
    }
    state_ = State::accepted;
    emit(ReplayProgressKind::request_accepted, 0, 0);
    return true;
}

bool ReplayProgressPublisher::publish_start() noexcept {
    if (state_ != State::accepted) {
        return false;
    }
    state_ = State::started;
    emit(ReplayProgressKind::replay_started, 0, 0);
    return true;
}

bool ReplayProgressPublisher::publish_input(const std::uint32_t recording_frame) noexcept {
    if (state_ != State::started || recording_frame != inputs_consumed_ ||
        inputs_consumed_ >= total_inputs_ || inputs_consumed_ != frames_completed_) {
        return false;
    }
    ++inputs_consumed_;
    emit(ReplayProgressKind::input_consumed, recording_frame, 0);
    return true;
}

bool ReplayProgressPublisher::publish_frame(const std::uint32_t recording_frame,
                                            const std::uint64_t engine_frame) noexcept {
    if (state_ != State::started || recording_frame != frames_completed_ ||
        inputs_consumed_ != frames_completed_ + 1U) {
        return false;
    }
    ++frames_completed_;
    last_engine_frame_ = engine_frame;
    emit(ReplayProgressKind::frame_completed, recording_frame, engine_frame);
    return true;
}

bool ReplayProgressPublisher::publish_finish() noexcept {
    if (state_ != State::started || inputs_consumed_ != total_inputs_ ||
        frames_completed_ != total_inputs_) {
        return false;
    }
    state_ = State::finished;
    emit(ReplayProgressKind::replay_finished, total_inputs_, 0);
    return true;
}

bool ReplayProgressPublisher::accepted() const noexcept { return state_ != State::pending; }

bool ReplayProgressPublisher::started() const noexcept {
    return state_ == State::started || state_ == State::finished;
}

bool ReplayProgressPublisher::finished() const noexcept { return state_ == State::finished; }

std::uint32_t ReplayProgressPublisher::inputs_consumed() const noexcept { return inputs_consumed_; }

std::uint32_t ReplayProgressPublisher::frames_completed() const noexcept {
    return frames_completed_;
}

std::uint64_t ReplayProgressPublisher::last_engine_frame() const noexcept {
    return last_engine_frame_;
}

void ReplayProgressPublisher::emit(const ReplayProgressKind kind,
                                   const std::uint32_t recording_frame,
                                   const std::uint64_t engine_frame) const noexcept {
    observer_(context_, {kind, total_inputs_, inputs_consumed_, frames_completed_, recording_frame,
                         engine_frame});
}

} // namespace ayther::audio_qa
