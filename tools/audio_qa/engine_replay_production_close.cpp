#include "engine_replay_production_close.h"

#include <ayther/ayther_session.h>

namespace ayther::audio_qa {
namespace {

ReplayProductionLimit freeze(void *const value) noexcept {
    if (value == nullptr) {
        return {false,
                false,
                false,
                0,
                0,
                0,
                0,
                0,
                "engine_session_missing",
                "Engine session is unavailable while freezing production"};
    }
    const auto limit = static_cast<AytherSession *>(value)->freeze_audio_production();
    return {true,
            limit.frozen,
            limit.complete,
            limit.last_emulation_frame,
            limit.main_sample_limit,
            limit.pending_main_frames,
            limit.delivered_output_samples_at_freeze,
            limit.auxiliary_input_limit,
            "ok",
            {}};
}

ReplayVoiceFinalization finalize(void *const value) noexcept {
    if (value == nullptr) {
        return {false,
                false,
                false,
                0,
                0,
                0,
                "engine_session_missing",
                "Engine session is unavailable while finalizing voices"};
    }
    const auto result = static_cast<AytherSession *>(value)->finalize_audio_hd_voices_for_test();
    return {true,
            result.accepted,
            result.output_position_known,
            result.finalized_voices,
            result.output_position,
            result.frame_position,
            "ok",
            {}};
}

ReplayFrozenDrain drain(void *const value) noexcept {
    if (value == nullptr) {
        return {false,
                false,
                false,
                0,
                0,
                0,
                "engine_session_missing",
                "Engine session is unavailable while draining frozen audio"};
    }
    const auto result = static_cast<AytherSession *>(value)->drain_frozen_audio();
    return {true,
            result.accepted,
            result.complete,
            result.drained_main_frames,
            result.remaining_main_frames,
            result.main_sample_limit,
            "ok",
            {},
            result.output_complete,
            result.output_sample_limit};
}

} // namespace

ReplayProductionCloseOperations
engine_replay_production_close_operations(void *const session) noexcept {
    if (session == nullptr) {
        return {};
    }
    return {session, freeze, finalize, drain};
}

} // namespace ayther::audio_qa
