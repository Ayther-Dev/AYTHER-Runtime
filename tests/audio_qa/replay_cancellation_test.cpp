#include "initial_state_publication.h"
#include "recording_replay_loop.h"
#include "replay_cancellation.h"
#include "replay_progress.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class T> qa::InitialCollection<T> empty_collection() {
    return {{qa::Availability::known, std::vector<T>{}, {}},
            {qa::Availability::known, qa::FactId{"run-114", "initial-state", 1}, {}}};
}

qa::InitialState initial_state() {
    qa::InitialState state;
    state.run_id = "run-114";
    state.game_state_id = "game-state-114";
    state.game_restore_result = qa::RestoreResult::succeeded;
    state.hd_initialization = qa::HdInitialization::fresh;
    state.initialization_reason = "hd_state_absent";
    state.detector = {qa::Availability::known, qa::DetectorInitial{true, 0, 0, {}}, {}};
    state.detector_observed_by = {
        qa::Availability::known, qa::FactId{"run-114", "initial-state", 1}, {}};
    state.windows = empty_collection<qa::InitialWindow>();
    state.voices = empty_collection<qa::Occurrence>();
    state.requests = empty_collection<qa::InitialRequest>();
    state.pending_audio = empty_collection<qa::PendingAudio>();
    return state;
}

qa::RecordingInputSource input_source() {
    static constexpr std::array bytes{std::byte{0x01}, std::byte{0x00}, std::byte{0x02},
                                      std::byte{0x00}};
    qa::RecordingLayout layout;
    layout.frame_count = 2;
    layout.inputs = {0, bytes.size()};
    auto result = qa::make_recording_input_source(bytes, layout);
    if (result.error != qa::RecordingInputSourceError::none) {
        throw std::runtime_error("input_fixture_rejected");
    }
    return result.source;
}

bool accept(void *, const qa::InitialState &) noexcept { return true; }
void observe(void *, const qa::ReplayProgressEvent &) noexcept {}

struct Operations {
    std::uint32_t set_calls{};
    std::uint32_t step_calls{};

    static qa::ReplayFrameOperationResult set(void *const context, const std::uint32_t,
                                              const std::uint16_t) noexcept {
        auto &operations = *static_cast<Operations *>(context);
        ++operations.set_calls;
        return {true, 500 + operations.step_calls, "ok", {}};
    }

    static qa::ReplayFrameOperationResult step(void *const context) noexcept {
        auto &operations = *static_cast<Operations *>(context);
        ++operations.step_calls;
        return {true, 500 + operations.step_calls, "ok", {}};
    }
};

qa::RunLifecycle playing_lifecycle() {
    qa::Run run;
    run.run_id = "run-114";
    run.request_id = "request-114";
    run.take_id = "take-main";
    qa::RunLifecycle lifecycle{run};
    if (lifecycle.mark_ready() != qa::RunTransitionError::none ||
        lifecycle.start_playback() != qa::RunTransitionError::none) {
        throw std::runtime_error("lifecycle_fixture_failed");
    }
    return lifecycle;
}

} // namespace

int main() {
    try {
        auto inputs = input_source();
        qa::InitialStatePublication publication{initial_state()};
        require(publication.publish(nullptr, accept), "initial_state_publication_failed");
        qa::ReplayProgressPublisher progress{2, nullptr, observe};
        require(progress.publish_acceptance(), "acceptance_publication_failed");
        qa::RecordingReplayLoop loop{publication, inputs, &progress};
        Operations operations;
        const qa::ReplayFrameOperations callbacks{&operations, Operations::set, Operations::step};

        const auto first = loop.execute_next(callbacks);
        require(first.error == qa::ReplayFrameError::none && loop.frames_completed() == 1 &&
                    progress.last_engine_frame() == 501,
                "first_frame_did_not_complete");

        qa::ReplayControlGate controls;
        const auto requested = controls.handle({qa::ReplayExternalControl::cancel, 0});
        qa::ReplayCancellation cancellation;
        require(requested.disposition == qa::ReplayControlDisposition::cancel_requested &&
                    cancellation.capture_request(controls, loop.frames_completed(),
                                                 progress.last_engine_frame()),
                "cancellation_request_was_not_captured");

        auto lifecycle = playing_lifecycle();
        require(cancellation.apply_at_frame_boundary(loop.frames_completed(),
                                                     progress.last_engine_frame(), lifecycle),
                "cancellation_was_not_applied_at_boundary");
        const auto &record = cancellation.record();
        require(record.requested && record.applied && record.requested_after_frames == 1 &&
                    record.applied_after_frames == 1 &&
                    record.last_executed_recording_frame == std::uint32_t{0} &&
                    record.last_engine_frame == std::uint64_t{501} &&
                    lifecycle.run().phase == qa::Phase::closing &&
                    lifecycle.run().playback_result == qa::PlaybackResult::cancelled &&
                    inputs.consumed() == 1 && loop.frames_completed() == 1 &&
                    operations.set_calls == 1 && operations.step_calls == 1,
                "cancellation_changed_the_last_completed_position");

        std::puts("replay_cancellation_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "replay_cancellation_test: %s\n", error.what());
        return 1;
    }
}
