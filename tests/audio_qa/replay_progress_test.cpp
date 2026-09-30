#include "initial_state_publication.h"
#include "recording_replay_loop.h"
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
            {qa::Availability::known, qa::FactId{"run-106", "initial-state", 1}, {}}};
}

qa::InitialState complete_fresh_state() {
    qa::InitialState state;
    state.run_id = "run-106";
    state.game_state_id = "game-state-106";
    state.game_restore_result = qa::RestoreResult::succeeded;
    state.hd_initialization = qa::HdInitialization::fresh;
    state.initialization_reason = "hd_state_absent";
    state.detector = {qa::Availability::known, qa::DetectorInitial{true, 0, 0, {}}, {}};
    state.detector_observed_by = {
        qa::Availability::known, qa::FactId{"run-106", "initial-state", 1}, {}};
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

struct Capture {
    std::array<qa::ReplayProgressEvent, 7> events{};
    std::size_t size{};

    static void receive(void *const value, const qa::ReplayProgressEvent &event) noexcept {
        auto &capture = *static_cast<Capture *>(value);
        if (capture.size < capture.events.size()) {
            capture.events[capture.size++] = event;
        }
    }
};

struct Operations {
    std::uint32_t set_calls{};
    std::uint32_t step_calls{};

    static qa::ReplayFrameOperationResult set(void *const value, const std::uint32_t,
                                              const std::uint16_t) noexcept {
        auto &operations = *static_cast<Operations *>(value);
        ++operations.set_calls;
        return {true, operations.step_calls, "ok", {}};
    }

    static qa::ReplayFrameOperationResult step(void *const value) noexcept {
        auto &operations = *static_cast<Operations *>(value);
        ++operations.step_calls;
        return {true, 900U + operations.step_calls, "ok", {}};
    }
};

} // namespace

int main() {
    try {
        auto inputs = input_source();
        qa::InitialStatePublication publication{complete_fresh_state()};
        require(publication.publish(nullptr, accept), "initial_state_publication_failed");
        Capture capture;
        qa::ReplayProgressPublisher progress{inputs.frame_count(), &capture, Capture::receive};
        require(progress.publish_acceptance() && capture.size == 1 &&
                    capture.events[0].kind == qa::ReplayProgressKind::request_accepted &&
                    !progress.started() && inputs.consumed() == 0,
                "acceptance_was_confused_with_effective_start");

        qa::RecordingReplayLoop loop{publication, inputs, &progress};
        Operations operations;
        const qa::ReplayFrameOperations callbacks{&operations, Operations::set, Operations::step};
        auto result = loop.execute_next(callbacks);
        require(result.error == qa::ReplayFrameError::none && capture.size == 4 &&
                    capture.events[1].kind == qa::ReplayProgressKind::replay_started &&
                    capture.events[2].kind == qa::ReplayProgressKind::input_consumed &&
                    capture.events[3].kind == qa::ReplayProgressKind::frame_completed,
                "first_frame_progress_order_is_wrong");
        result = loop.execute_next(callbacks);
        require(result.error == qa::ReplayFrameError::none && capture.size == 6 &&
                    progress.inputs_consumed() == 2 && progress.frames_completed() == 2 &&
                    operations.set_calls == 2 && operations.step_calls == 2,
                "n_inputs_and_frames_were_not_counted");
        result = loop.execute_next(callbacks);
        require(result.error == qa::ReplayFrameError::input_exhausted && capture.size == 7 &&
                    capture.events[6].kind == qa::ReplayProgressKind::replay_finished &&
                    capture.events[6].inputs_consumed == 2 &&
                    capture.events[6].frames_completed == 2 && progress.finished() &&
                    operations.set_calls == 2 && operations.step_calls == 2,
                "finish_was_not_published_at_exact_n");

        Capture rejected_capture;
        qa::ReplayProgressPublisher rejected{2, &rejected_capture, Capture::receive};
        require(!rejected.publish_start() && rejected_capture.size == 0 && !rejected.started(),
                "effective_start_was_published_before_acceptance");

        std::puts("replay_progress_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "replay_progress_test: %s\n", error.what());
        return 1;
    }
}
