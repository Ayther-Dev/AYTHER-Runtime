#include "presentation_health.h"

#include <utility>

namespace ayther::replay_inspection {

void PresentationHealth::interrupt(std::string cause) {
    if (state_ == PresentationState::lost)
        return;
    if (state_ == PresentationState::healthy) {
        ++incidents_;
        cause_ = std::move(cause);
    }
    state_ = PresentationState::interrupted;
}

void PresentationHealth::frame_result(VideoResult result) {
    switch (result) {
    case VideoResult::presented:
        return;
    case VideoResult::acquire_failed:
        video_lost_ = true;
        interrupt("video_acquire_failed");
        return;
    case VideoResult::present_failed:
        video_lost_ = true;
        interrupt("video_present_failed");
        return;
    case VideoResult::device_lost:
        if (state_ == PresentationState::healthy)
            ++incidents_;
        cause_ = "device_lost";
        state_ = PresentationState::lost;
        return;
    }
}

void PresentationHealth::audio_device_removed(bool in_use) {
    if (!in_use)
        return;
    audio_lost_ = true;
    interrupt("audio_device_removed");
}

void PresentationHealth::audio_device_added() noexcept {}

void PresentationHealth::minimized(bool value) {
    minimized_ = value;
    if (value)
        interrupt("window_minimized");
}

void PresentationHealth::focus(bool) noexcept {}

RecoveryAttempt PresentationHealth::poll(bool swapchain_ready, bool audio_ready) {
    switch (state_) {
    case PresentationState::healthy:
        return RecoveryAttempt::healthy;
    case PresentationState::lost:
        return RecoveryAttempt::unrecoverable;
    case PresentationState::interrupted:
        break;
    }
    if (minimized_ || (video_lost_ && !swapchain_ready) || (audio_lost_ && !audio_ready))
        return RecoveryAttempt::still_interrupted;
    video_lost_ = false;
    audio_lost_ = false;
    state_ = PresentationState::healthy;
    return RecoveryAttempt::recovered;
}

} // namespace ayther::replay_inspection
