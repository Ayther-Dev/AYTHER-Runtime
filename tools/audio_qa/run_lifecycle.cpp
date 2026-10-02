#include "run_lifecycle.h"

#include <utility>

namespace ayther::audio_qa {

RunLifecycle::RunLifecycle(Run run) noexcept : run_(std::move(run)) {
    if (run_.run_id.empty() || run_.request_id.empty() || run_.take_id.empty() ||
        run_.phase != Phase::preparing || run_.playback_result != PlaybackResult::not_started ||
        run_.evidence_result != EvidenceResult::pending) {
        error_ = RunTransitionError::invalid_initial_state;
    }
}

RunTransitionError RunLifecycle::mark_ready() noexcept {
    if (error_ != RunTransitionError::none || run_.phase != Phase::preparing) {
        return RunTransitionError::invalid_transition;
    }
    run_.phase = Phase::ready;
    return RunTransitionError::none;
}

RunTransitionError RunLifecycle::start_playback() noexcept {
    if (error_ != RunTransitionError::none || run_.phase != Phase::ready) {
        return RunTransitionError::invalid_transition;
    }
    run_.phase = Phase::playing;
    run_.playback_result = PlaybackResult::in_progress;
    return RunTransitionError::none;
}

RunTransitionError RunLifecycle::begin_closing(const PlaybackResult playback_result) noexcept {
    if (error_ != RunTransitionError::none || run_.phase != Phase::playing) {
        return RunTransitionError::invalid_transition;
    }
    if (playback_result == PlaybackResult::not_started ||
        playback_result == PlaybackResult::in_progress) {
        return RunTransitionError::invalid_terminal_result;
    }
    run_.phase = Phase::closing;
    run_.playback_result = playback_result;
    return RunTransitionError::none;
}

RunTransitionError RunLifecycle::finish_closing(const EvidenceResult evidence_result) noexcept {
    if (error_ != RunTransitionError::none || run_.phase != Phase::closing) {
        return RunTransitionError::invalid_transition;
    }
    if (evidence_result == EvidenceResult::pending ||
        evidence_result == EvidenceResult::in_progress) {
        return RunTransitionError::invalid_evidence_result;
    }
    run_.phase = Phase::closed;
    run_.evidence_result = evidence_result;
    return RunTransitionError::none;
}

const Run &RunLifecycle::run() const noexcept { return run_; }

RunTransitionError RunLifecycle::error() const noexcept { return error_; }

} // namespace ayther::audio_qa
