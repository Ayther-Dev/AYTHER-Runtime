#include "initial_state_publication.h"

#include <utility>

namespace ayther::audio_qa {

InitialStatePublication::InitialStatePublication(InitialState state) : state_(std::move(state)) {}

bool InitialStatePublication::publish(void *const context,
                                      const InitialStateConsumer consumer) noexcept {
    if (status_ == InitialStatePublicationStatus::acknowledged) {
        return true;
    }
    if (status_ == InitialStatePublicationStatus::failed) {
        return false;
    }
    if (!well_formed(state_)) {
        status_ = InitialStatePublicationStatus::failed;
        error_ = InitialStatePublicationError::invalid_state;
        return false;
    }
    if (state_.game_restore_result != RestoreResult::succeeded ||
        !has_complete_hd_description(state_)) {
        status_ = InitialStatePublicationStatus::failed;
        error_ = InitialStatePublicationError::incomplete_state;
        return false;
    }
    if (consumer == nullptr) {
        status_ = InitialStatePublicationStatus::failed;
        error_ = InitialStatePublicationError::consumer_unavailable;
        return false;
    }
    if (!consumer(context, state_)) {
        status_ = InitialStatePublicationStatus::failed;
        error_ = InitialStatePublicationError::consumer_rejected;
        return false;
    }
    status_ = InitialStatePublicationStatus::acknowledged;
    return true;
}

std::optional<RecordingFrameInput>
InitialStatePublication::next_input(RecordingInputSource &inputs) noexcept {
    if (!input_permitted()) {
        return std::nullopt;
    }
    return inputs.next();
}

InitialStatePublicationStatus InitialStatePublication::status() const noexcept { return status_; }

InitialStatePublicationError InitialStatePublication::error() const noexcept { return error_; }

bool InitialStatePublication::input_permitted() const noexcept {
    return status_ == InitialStatePublicationStatus::acknowledged;
}

const InitialState &InitialStatePublication::state() const noexcept { return state_; }

} // namespace ayther::audio_qa
