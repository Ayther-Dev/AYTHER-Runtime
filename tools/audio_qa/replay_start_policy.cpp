#include "replay_start_policy.h"

namespace ayther::audio_qa {

ReplayStartDecision
decide_replay_start(const ReplayLaunchKind launch,
                    const RecordingReplayPreparationResult &preparation) noexcept {
    if (launch == ReplayLaunchKind::ordinary_game) {
        return {true, false, true,
                preparation.restore.succeeded ? "none" : "ordinary_restore_fallback"};
    }
    if (preparation.state_error != RecordingStateError::none) {
        return {false, false, false, "qa.game_state_preparation_failed"};
    }
    if (!preparation.restore_attempted) {
        return {false, false, false, "qa.game_state_restore_not_attempted"};
    }
    if (!preparation.restore.succeeded || !preparation.inputs.has_value()) {
        return {false, false, false, "qa.game_state_restore_failed"};
    }
    return {false, true, true, "none"};
}

} // namespace ayther::audio_qa
