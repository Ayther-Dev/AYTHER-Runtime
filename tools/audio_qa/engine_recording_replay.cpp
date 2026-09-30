#include "engine_recording_replay.h"

#include <ayther/ayther_session.h>

namespace ayther::audio_qa {
namespace {

[[nodiscard]] ReplayFrameOperationResult set_recorded_input(void *const value, const std::uint32_t,
                                                            const std::uint16_t buttons) noexcept {
    if (value == nullptr) {
        return {false, 0, "engine_session_missing",
                "engine session is unavailable for recorded input"};
    }
    static_cast<AytherSession *>(value)->set_input(0, buttons);
    return {true, 0, "ok", {}};
}

[[nodiscard]] ReplayFrameOperationResult step_recorded_frame(void *const value) noexcept {
    if (value == nullptr) {
        return {false, 0, "engine_session_missing", "engine session is unavailable for frame step"};
    }
    try {
        const auto &frame = static_cast<AytherSession *>(value)->step();
        return {true, frame.frame_index, "ok", {}};
    } catch (...) {
        return {false, 0, "engine_step_failed", "Engine raised while stepping the recorded frame"};
    }
}

} // namespace

ReplayFrameOperations engine_recording_replay_operations(void *const session) noexcept {
    if (session == nullptr) {
        return {};
    }
    return {session, set_recorded_input, step_recorded_frame};
}

} // namespace ayther::audio_qa
