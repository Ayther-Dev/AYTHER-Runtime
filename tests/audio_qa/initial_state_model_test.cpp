#include "initial_state.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
template <class T> qa::InitialCollection<T> empty_collection() {
    return {{qa::Availability::known, std::vector<T>{}, {}},
            {qa::Availability::known, qa::FactId{"run", "initial-state", 1}, {}}};
}
qa::InitialState fresh() {
    qa::InitialState state;
    state.run_id = "run";
    state.game_state_id = "game-state-content";
    state.game_restore_result = qa::RestoreResult::succeeded;
    state.hd_initialization = qa::HdInitialization::fresh;
    state.initialization_reason = "hd_state_absent";
    state.detector = {qa::Availability::known, qa::DetectorInitial{true, 0, 0, {}}, {}};
    state.detector_observed_by = {
        qa::Availability::known, qa::FactId{"run", "initial-state", 1}, {}};
    state.windows = empty_collection<qa::InitialWindow>();
    state.voices = empty_collection<qa::Occurrence>();
    state.requests = empty_collection<qa::InitialRequest>();
    state.pending_audio = empty_collection<qa::PendingAudio>();
    return state;
}
} // namespace

int main() {
    try {
        auto state = fresh();
        require(qa::well_formed(state) && qa::has_complete_hd_description(state),
                "verified_fresh_state_rejected");
        state.voices.observed_by = {};
        require(qa::well_formed(state) && !qa::has_complete_hd_description(state),
                "empty_unverified_list_counted_as_fresh");
        state = fresh();
        state.detector.value->pending_inputs = 1;
        require(!qa::has_complete_hd_description(state), "inherited_detector_inputs_ignored");
        state = fresh();
        state.hd_version = {2, 0};
        require(qa::well_formed(state) && !qa::has_complete_hd_description(state),
                "unknown_hd_version_accepted");
        state = fresh();
        state.supplied_hd_version = {qa::Availability::known, qa::ContractVersion{2, 0}, {}};
        state.hd_restore_result = qa::RestoreResult::failed;
        state.initialization_reason = "incompatible_supplied_hd_state";
        require(qa::has_complete_hd_description(state), "fresh_fallback_description_rejected");

        state = fresh();
        state.hd_initialization = qa::HdInitialization::restored;
        state.hd_restore_result = qa::RestoreResult::succeeded;
        qa::StateImage image{"engine-image", qa::hd_state_schema_version, {}};
        image.content.byte_size = 1024;
        state.engine_state_image = {qa::Availability::known, image, {}};
        image.image_id = "detector-image";
        state.detector.value->reset_verified = false;
        state.detector.value->continuation_state = {qa::Availability::known, image, {}};
        qa::Occurrence voice;
        voice.id = {"run", 1};
        voice.business_key = 101;
        voice.track_id = {qa::Availability::known, "track", {}};
        voice.position = {qa::Availability::known, qa::TrackPosition{614, 44100}, {}};
        voice.gain = {qa::Availability::known, 0.5F, {}};
        voice.muted = {qa::Availability::known, false, {}};
        voice.start_causes = {qa::PreexistingContext{"supplied-state"}};
        state.voices.entries.value->push_back(voice);
        voice.id.sequence = 2;
        state.voices.entries.value->push_back(voice);
        require(qa::has_complete_hd_description(state), "active_reused_key_state_rejected");
        state.voices.entries.value->at(0).position = {};
        require(!qa::has_complete_hd_description(state),
                "restored_voice_without_position_accepted");
        state.voices.entries.value->at(0).position = voice.position;
        qa::PendingAudio pending;
        pending.queue_id = "main";
        pending.format = {qa::PcmFormat::s16le, 44100, 2};
        pending.range = {"output", 44100, 8, 12};
        pending.pcm_content.byte_size = 16;
        state.pending_audio.entries.value->push_back(pending);
        require(!qa::has_complete_hd_description(state), "pending_audio_phase_omission_accepted");
        state.pending_audio.entries.value->at(0).continuation_state = {
            qa::Availability::known, image, {}};
        require(qa::has_complete_hd_description(state), "pending_audio_description_rejected");
        state.pending_audio.entries.value->at(0).pcm_content.byte_size = 15;
        require(!qa::well_formed(state), "partial_pending_frame_accepted");

        state = fresh();
        state.windows.entries.value->resize(qa::max_initial_windows);
        require(qa::well_formed(state), "window_limit_boundary_rejected");
        state.windows.entries.value->push_back({});
        require(!qa::well_formed(state), "window_limit_ignored");
        state = fresh();
        state.voices.entries.value->assign(qa::max_mix_participants, voice);
        require(qa::well_formed(state), "voice_count_boundary_rejected");
        state.voices.entries.value->push_back(voice);
        require(!qa::well_formed(state), "voice_count_limit_ignored");
        state = fresh();
        state.requests.entries.value->assign(qa::max_initial_requests,
                                             {"request", 101, "track", {}});
        require(qa::well_formed(state), "request_count_boundary_rejected");
        state.requests.entries.value->push_back({"extra", 101, "track", {}});
        require(!qa::well_formed(state), "request_count_limit_ignored");
        state = fresh();
        state.pending_audio.entries.value->assign(qa::max_initial_audio_queues, pending);
        require(qa::well_formed(state), "audio_queue_boundary_rejected");
        state.pending_audio.entries.value->push_back(pending);
        require(!qa::well_formed(state), "audio_queue_limit_ignored");
        state = fresh();
        image.content.byte_size = qa::max_initial_state_bytes;
        state.engine_state_image = {qa::Availability::known, image, {}};
        require(qa::well_formed(state), "state_size_boundary_rejected");
        ++state.engine_state_image.value->content.byte_size;
        require(!qa::well_formed(state), "state_size_limit_ignored");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "qa_initial_state_test_failed: %s\n", error.what());
        return 1;
    }
}
