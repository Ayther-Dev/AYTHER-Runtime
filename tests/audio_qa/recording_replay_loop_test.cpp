#include "recording_replay_loop.h"

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
            {qa::Availability::known, qa::FactId{"run-103", "initial-state", 1}, {}}};
}

qa::InitialState complete_fresh_state() {
    qa::InitialState state;
    state.run_id = "run-103";
    state.game_state_id = "game-state-103";
    state.game_restore_result = qa::RestoreResult::succeeded;
    state.hd_initialization = qa::HdInitialization::fresh;
    state.initialization_reason = "hd_state_absent";
    state.detector = {qa::Availability::known, qa::DetectorInitial{true, 0, 0, {}}, {}};
    state.detector_observed_by = {
        qa::Availability::known, qa::FactId{"run-103", "initial-state", 1}, {}};
    state.windows = empty_collection<qa::InitialWindow>();
    state.voices = empty_collection<qa::Occurrence>();
    state.requests = empty_collection<qa::InitialRequest>();
    state.pending_audio = empty_collection<qa::PendingAudio>();
    return state;
}

qa::RecordingInputSource input_source() {
    static constexpr std::array bytes{std::byte{0x01}, std::byte{0x00}, std::byte{0xff},
                                      std::byte{0x80}};
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

struct Operations {
    unsigned set_calls{};
    unsigned step_calls{};
    std::array<std::uint32_t, 2> frames{};
    std::array<std::uint16_t, 2> buttons{};
    bool fail_step{};

    static qa::ReplayFrameOperationResult set(void *const value, const std::uint32_t frame,
                                              const std::uint16_t buttons) noexcept {
        auto &operations = *static_cast<Operations *>(value);
        const auto index = operations.set_calls++;
        if (index >= operations.frames.size()) {
            return {false, 0, "too_many_set_input_calls", {}};
        }
        operations.frames[index] = frame;
        operations.buttons[index] = buttons;
        return {true, 0, "ok", {}};
    }

    static qa::ReplayFrameOperationResult step(void *const value) noexcept {
        auto &operations = *static_cast<Operations *>(value);
        ++operations.step_calls;
        if (operations.fail_step) {
            return {false, 700, "step_failed", "synthetic failure"};
        }
        return {true, 700 + operations.step_calls, "ok", {}};
    }
};

} // namespace

int main() {
    try {
        auto blocked_inputs = input_source();
        qa::InitialStatePublication blocked_publication{complete_fresh_state()};
        qa::RecordingReplayLoop blocked{blocked_publication, blocked_inputs};
        Operations blocked_operations;
        auto result =
            blocked.execute_next({&blocked_operations, Operations::set, Operations::step});
        require(result.error == qa::ReplayFrameError::initial_state_not_acknowledged &&
                    blocked_inputs.consumed() == 0 && blocked_operations.set_calls == 0 &&
                    blocked_operations.step_calls == 0,
                "frame_executed_before_initial_state");

        auto inputs = input_source();
        qa::InitialStatePublication publication{complete_fresh_state()};
        require(publication.publish(nullptr, accept), "initial_state_publication_failed");
        qa::RecordingReplayLoop loop{publication, inputs};
        Operations operations;
        const qa::ReplayFrameOperations callbacks{&operations, Operations::set, Operations::step};

        result = loop.execute_next(callbacks);
        require(result.error == qa::ReplayFrameError::none && result.input &&
                    result.input->frame == 0 && result.input->buttons == 0x0001 &&
                    result.engine_frame == 701 && loop.frames_completed() == 1 &&
                    operations.set_calls == 1 && operations.step_calls == 1,
                "first_frame_did_not_use_one_set_and_step");
        result = loop.execute_next(callbacks);
        require(result.error == qa::ReplayFrameError::none && result.input &&
                    result.input->frame == 1 && result.input->buttons == 0x80ff &&
                    result.engine_frame == 702 && loop.frames_completed() == 2 &&
                    operations.set_calls == 2 && operations.step_calls == 2,
                "second_frame_did_not_use_one_set_and_step");
        result = loop.execute_next(callbacks);
        require(result.error == qa::ReplayFrameError::input_exhausted &&
                    operations.set_calls == 2 && operations.step_calls == 2 && !loop.failed(),
                "exhausted_input_executed_an_extra_frame");

        auto unavailable_inputs = input_source();
        qa::InitialStatePublication unavailable_publication{complete_fresh_state()};
        require(unavailable_publication.publish(nullptr, accept),
                "unavailable_fixture_publication_failed");
        qa::RecordingReplayLoop unavailable{unavailable_publication, unavailable_inputs};
        result = unavailable.execute_next({});
        require(result.error == qa::ReplayFrameError::operations_unavailable &&
                    unavailable_inputs.consumed() == 0 && unavailable.failed(),
                "missing_operations_consumed_input");

        auto failed_inputs = input_source();
        qa::InitialStatePublication failed_publication{complete_fresh_state()};
        require(failed_publication.publish(nullptr, accept), "failure_fixture_publication_failed");
        qa::RecordingReplayLoop failed{failed_publication, failed_inputs};
        Operations failed_operations;
        failed_operations.fail_step = true;
        result = failed.execute_next({&failed_operations, Operations::set, Operations::step});
        require(result.error == qa::ReplayFrameError::step_failed &&
                    result.operation_code == "step_failed" && failed_inputs.consumed() == 1 &&
                    failed.frames_completed() == 0 && failed.failed(),
                "step_failure_not_preserved");
        result = failed.execute_next({&failed_operations, Operations::set, Operations::step});
        require(result.error == qa::ReplayFrameError::loop_failed &&
                    failed_operations.set_calls == 1 && failed_operations.step_calls == 1 &&
                    failed_inputs.consumed() == 1,
                "failed_loop_executed_another_frame");

        std::puts("recording_replay_loop_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "recording_replay_loop_test: %s\n", error.what());
        return 1;
    }
}
