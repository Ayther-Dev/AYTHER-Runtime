#include "progress_watchdog.h"

#include <utility>

namespace ayther::audio_qa {

ProgressWatchdog::ProgressWatchdog(const WatchdogPhase phase,
                                   const std::uint64_t started_ms) noexcept
    : phase_(phase), started_ms_(started_ms), last_observed_ms_(started_ms),
      last_progress_ms_(started_ms) {}

bool ProgressWatchdog::observe_heartbeat(const std::uint64_t now_ms) noexcept {
    if (!valid_time(now_ms)) {
        return false;
    }
    last_observed_ms_ = now_ms;
    return true;
}

bool ProgressWatchdog::observe_preparation_stage(std::string stage, const std::uint64_t now_ms) {
    if (phase_ != WatchdogPhase::preparation || stage.empty() || !valid_time(now_ms)) {
        return false;
    }
    last_observed_ms_ = now_ms;
    if (stage == last_preparation_stage_) {
        return true;
    }
    last_preparation_stage_ = std::move(stage);
    last_progress_ms_ = now_ms;
    return true;
}

bool ProgressWatchdog::observe_completed_frame(const std::uint32_t frame,
                                               const std::uint64_t now_ms) noexcept {
    if (phase_ != WatchdogPhase::playback || !valid_time(now_ms)) {
        return false;
    }
    last_observed_ms_ = now_ms;
    if (!last_completed_frame_.has_value() || frame > *last_completed_frame_) {
        last_completed_frame_ = frame;
        last_progress_ms_ = now_ms;
    }
    return true;
}

bool ProgressWatchdog::observe_bytes_received(const std::uint64_t bytes,
                                              const std::uint64_t now_ms) noexcept {
    if (phase_ != WatchdogPhase::closing || !valid_time(now_ms) || bytes < bytes_received_) {
        return false;
    }
    last_observed_ms_ = now_ms;
    bytes_received_ = bytes;
    return true;
}

bool ProgressWatchdog::observe_bytes_durable(const std::uint64_t bytes,
                                             const std::uint64_t now_ms) noexcept {
    if (phase_ != WatchdogPhase::closing || !valid_time(now_ms) || bytes < bytes_durable_ ||
        bytes > bytes_received_) {
        return false;
    }
    last_observed_ms_ = now_ms;
    if (bytes > bytes_durable_) {
        bytes_durable_ = bytes;
        last_progress_ms_ = now_ms;
    }
    return true;
}

std::optional<WatchdogTimeout> ProgressWatchdog::check(const std::uint64_t now_ms) const noexcept {
    if (!valid_time(now_ms)) {
        return {};
    }
    const auto active_policy = policy();
    const auto total_elapsed = now_ms - started_ms_;
    std::optional<WatchdogTimeoutReason> reason;
    if (active_policy.total_ms != 0 && total_elapsed >= active_policy.total_ms) {
        reason = WatchdogTimeoutReason::total_time;
    } else if (now_ms - last_progress_ms_ >= active_policy.no_progress_ms) {
        reason = WatchdogTimeoutReason::no_progress;
    }
    if (!reason.has_value()) {
        return {};
    }
    return WatchdogTimeout{phase_,
                           *reason,
                           EvidenceResult::incomplete,
                           last_preparation_stage_,
                           last_completed_frame_,
                           bytes_received_,
                           bytes_durable_,
                           last_progress_ms_,
                           total_elapsed};
}

bool ProgressWatchdog::valid_time(const std::uint64_t now_ms) const noexcept {
    return now_ms >= started_ms_ && now_ms >= last_observed_ms_;
}

WatchdogPolicy ProgressWatchdog::policy() const noexcept {
    switch (phase_) {
    case WatchdogPhase::preparation:
        return preparation_watchdog_policy;
    case WatchdogPhase::playback:
        return playback_watchdog_policy;
    case WatchdogPhase::closing:
        return closing_watchdog_policy;
    }
    return preparation_watchdog_policy;
}

} // namespace ayther::audio_qa
