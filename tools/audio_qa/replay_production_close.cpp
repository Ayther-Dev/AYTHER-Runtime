#include "replay_production_close.h"

namespace ayther::audio_qa {

ReplayProductionCloseResult
close_replay_production(const ReplayProgressPublisher &progress,
                        const ReplayProductionCloseOperations &operations) noexcept {
    ReplayProductionCloseResult result;
    if (!progress.finished()) {
        result.error = ReplayProductionCloseError::replay_not_finished;
        return result;
    }
    if (operations.context == nullptr || operations.freeze == nullptr ||
        operations.finalize_voices == nullptr || operations.drain == nullptr) {
        result.error = ReplayProductionCloseError::operations_unavailable;
        return result;
    }

    result.limit = operations.freeze(operations.context);
    if (!result.limit.succeeded) {
        result.error = ReplayProductionCloseError::freeze_failed;
        return result;
    }
    if (!result.limit.frozen || result.limit.last_emulation_frame != progress.last_engine_frame()) {
        result.error = ReplayProductionCloseError::invalid_limit;
        return result;
    }

    result.finalization = operations.finalize_voices(operations.context);
    if (!result.finalization.succeeded || !result.finalization.accepted) {
        result.error = ReplayProductionCloseError::finalization_failed;
        return result;
    }

    result.drain = operations.drain(operations.context);
    if (!result.drain.succeeded || !result.drain.accepted) {
        result.error = ReplayProductionCloseError::drain_failed;
        return result;
    }
    if (!result.limit.complete || !result.drain.complete ||
        result.drain.remaining_main_frames != 0 ||
        result.drain.main_sample_limit != result.limit.main_sample_limit) {
        result.error = ReplayProductionCloseError::incomplete_drain;
    }
    return result;
}

} // namespace ayther::audio_qa
