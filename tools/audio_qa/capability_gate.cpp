#include "capability_gate.h"

namespace ayther::audio_qa {

bool CapabilityGate::negotiate(const CapabilitySet &offer) {
    if (state_ != CapabilityGateState::pending) {
        return false;
    }
    check_ = validate_capabilities(offer);
    state_ = check_.error == CapabilityError::none ? CapabilityGateState::accepted
                                                   : CapabilityGateState::rejected;
    return state_ == CapabilityGateState::accepted;
}

bool CapabilityGate::start_game(void *const context,
                                const StartQaGameOperation operation) noexcept {
    if (state_ != CapabilityGateState::accepted || operation == nullptr || !operation(context)) {
        return false;
    }
    state_ = CapabilityGateState::game_started;
    return true;
}

CapabilityGateState CapabilityGate::state() const noexcept { return state_; }

const CapabilityCheck &CapabilityGate::check() const noexcept { return check_; }

std::string_view CapabilityGate::diagnostic_code() const noexcept {
    switch (check_.error) {
    case CapabilityError::none:
        return state_ == CapabilityGateState::pending ? "capabilities_pending"
                                                      : "capabilities_accepted";
    case CapabilityError::malformed_offer:
        return "capabilities_malformed";
    case CapabilityError::incompatible_version:
        return "capability_version_incompatible";
    case CapabilityError::missing_capability:
        return "required_capability_missing";
    case CapabilityError::insufficient_limits:
        return "capability_limits_insufficient";
    }
    return "capabilities_unknown";
}

} // namespace ayther::audio_qa
