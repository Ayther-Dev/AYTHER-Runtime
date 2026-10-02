#include "replay_terminal_arbiter.h"

namespace ayther::audio_qa {
namespace {

bool valid(const ReplayTerminalEvent &event) noexcept {
    if (event.result == PlaybackResult::not_started ||
        event.result == PlaybackResult::in_progress) {
        return false;
    }
    if (event.frames_completed == 0) {
        return !event.last_executed_recording_frame.has_value() &&
               !event.last_engine_frame.has_value();
    }
    return event.last_executed_recording_frame == event.frames_completed - 1U &&
           event.last_engine_frame.has_value();
}

} // namespace

ReplayTerminalDecision ReplayTerminalArbiter::resolve(ReplayTerminalEvent event,
                                                      RunLifecycle &lifecycle) noexcept {
    if (!valid(event)) {
        return ReplayTerminalDecision::invalid;
    }
    if (terminal_.has_value()) {
        return ReplayTerminalDecision::already_resolved;
    }
    if (lifecycle.begin_closing(event.result) != RunTransitionError::none) {
        return ReplayTerminalDecision::invalid;
    }
    terminal_ = event;
    return ReplayTerminalDecision::accepted;
}

const std::optional<ReplayTerminalEvent> &ReplayTerminalArbiter::terminal() const noexcept {
    return terminal_;
}

} // namespace ayther::audio_qa
