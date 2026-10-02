#include "replay_control_gate.h"

namespace ayther::audio_qa {

ReplayControlResult ReplayControlGate::handle(const ReplayExternalControlCommand command) noexcept {
    switch (command.control) {
    case ReplayExternalControl::game_keyboard:
        return {ReplayControlDisposition::ignored, "game_input_ignored"};
    case ReplayExternalControl::rewind:
        return {ReplayControlDisposition::ignored, "rewind_ignored"};
    case ReplayExternalControl::fast_forward:
        return {ReplayControlDisposition::ignored, "fast_forward_ignored"};
    case ReplayExternalControl::cancel:
        cancellation_requested_ = true;
        return {ReplayControlDisposition::cancel_requested, "cancel_requested"};
    }
    return {ReplayControlDisposition::ignored, "external_control_ignored"};
}

bool ReplayControlGate::cancellation_requested() const noexcept { return cancellation_requested_; }

} // namespace ayther::audio_qa
