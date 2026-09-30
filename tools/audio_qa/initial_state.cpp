#include "initial_state.h"

#include <string_view>

namespace ayther::audio_qa {
namespace {
bool identifier(std::string_view value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}
bool valid(const Field<StateImage> &image) noexcept {
    return consistent_availability(image) &&
           (!image.value || (identifier(image.value->image_id) &&
                             image.value->content.byte_size <= max_initial_state_bytes));
}
bool proof(const Field<FactId> &field, std::string_view run) noexcept {
    return consistent_availability(field) &&
           (!field.value || (field.value->run_id == run && identifier(field.value->producer_id) &&
                             field.value->producer_sequence > 0));
}
template <class T>
bool valid(const InitialCollection<T> &collection, std::string_view run,
           std::size_t limit) noexcept {
    return consistent_availability(collection.entries) && proof(collection.observed_by, run) &&
           (!collection.entries.value || collection.entries.value->size() <= limit);
}
template <class T> bool described(const InitialCollection<T> &collection) noexcept {
    return collection.entries.value.has_value() && collection.observed_by.value.has_value();
}
bool versioned(const Field<StateImage> &image) noexcept {
    return image.value && image.value->version == hd_state_schema_version &&
           image.value->content.byte_size > 0;
}
} // namespace

bool well_formed(const InitialState &state) noexcept {
    if (!identifier(state.run_id) || !identifier(state.game_state_id) ||
        state.game_restore_result < RestoreResult::not_attempted ||
        state.game_restore_result > RestoreResult::failed ||
        state.hd_restore_result < RestoreResult::not_attempted ||
        state.hd_restore_result > RestoreResult::failed ||
        state.hd_initialization < HdInitialization::unknown ||
        state.hd_initialization > HdInitialization::restored ||
        state.initialization_reason.size() > max_identity_bytes ||
        !consistent_availability(state.supplied_hd_version) || !valid(state.engine_state_image) ||
        !consistent_availability(state.detector) ||
        !proof(state.detector_observed_by, state.run_id) ||
        !valid(state.windows, state.run_id, max_initial_windows) ||
        !valid(state.voices, state.run_id, max_mix_participants) ||
        !valid(state.requests, state.run_id, max_initial_requests) ||
        !valid(state.pending_audio, state.run_id, max_initial_audio_queues)) {
        return false;
    }
    if (state.detector.value && !valid(state.detector.value->continuation_state)) {
        return false;
    }
    if (state.windows.entries.value) {
        for (const auto &window : *state.windows.entries.value) {
            if (window.start_frame > window.end_frame) {
                return false;
            }
        }
    }
    if (state.voices.entries.value) {
        for (const auto &voice : *state.voices.entries.value) {
            if (!well_formed(voice) || voice.id.run_id != state.run_id) {
                return false;
            }
        }
    }
    if (state.requests.entries.value) {
        for (const auto &request : *state.requests.entries.value) {
            if (!identifier(request.request_id) || !identifier(request.track_id) ||
                !consistent_availability(request.scheduled_frame)) {
                return false;
            }
        }
    }
    if (state.pending_audio.entries.value) {
        for (const auto &pending : *state.pending_audio.entries.value) {
            if (!identifier(pending.queue_id) || !well_formed(pending.range) ||
                pending.range.sample_rate != pending.format.sample_rate ||
                pending.pcm_content.byte_size > max_initial_state_bytes ||
                !expected_payload_bytes(pending.format, 0) || !valid(pending.continuation_state)) {
                return false;
            }
            const auto frame_bytes = expected_payload_bytes(pending.format, 1);
            const auto frames = pending.range.end - pending.range.begin;
            if (!frame_bytes || frames > max_initial_state_bytes / *frame_bytes ||
                pending.pcm_content.byte_size != frames * *frame_bytes) {
                return false;
            }
        }
    }
    return true;
}

bool has_complete_hd_description(const InitialState &state) noexcept {
    if (!well_formed(state) || state.hd_version != hd_state_schema_version ||
        !state.detector.value || !state.detector_observed_by.value || !described(state.windows) ||
        !described(state.voices) || !described(state.requests) || !described(state.pending_audio)) {
        return false;
    }
    const auto &detector = *state.detector.value;
    if (state.hd_initialization == HdInitialization::fresh) {
        return !state.initialization_reason.empty() && detector.reset_verified &&
               detector.active_events == 0 && detector.pending_inputs == 0 &&
               state.windows.entries.value->empty() && state.voices.entries.value->empty() &&
               state.requests.entries.value->empty() &&
               state.pending_audio.entries.value->empty() &&
               state.hd_restore_result != RestoreResult::succeeded;
    }
    if (state.hd_initialization != HdInitialization::restored ||
        state.hd_restore_result != RestoreResult::succeeded ||
        !versioned(state.engine_state_image) || !versioned(detector.continuation_state)) {
        return false;
    }
    for (const auto &voice : *state.voices.entries.value) {
        if (!voice.position.value || !voice.track_id.value || !voice.gain.value ||
            !voice.muted.value) {
            return false;
        }
    }
    for (const auto &pending : *state.pending_audio.entries.value) {
        if (!versioned(pending.continuation_state)) {
            return false;
        }
    }
    return true;
}

} // namespace ayther::audio_qa
