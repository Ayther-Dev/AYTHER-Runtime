#include "initial_state_publication.h"
#include "recording_replay_loop.h"
#include "replay_control_gate.h"

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
            {qa::Availability::known, qa::FactId{"run-104", "initial-state", 1}, {}}};
}

qa::InitialState complete_fresh_state() {
    qa::InitialState state;
    state.run_id = "run-104";
    state.game_state_id = "game-state-104";
    state.game_restore_result = qa::RestoreResult::succeeded;
    state.hd_initialization = qa::HdInitialization::fresh;
    state.initialization_reason = "hd_state_absent";
    state.detector = {qa::Availability::known, qa::DetectorInitial{true, 0, 0, {}}, {}};
    state.detector_observed_by = {
        qa::Availability::known, qa::FactId{"run-104", "initial-state", 1}, {}};
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

struct Operations {
    std::array<std::uint16_t, 2> buttons{};
    std::uint32_t set_calls{};
    std::uint32_t step_calls{};

    static qa::ReplayFrameOperationResult set(void *const value, const std::uint32_t,
                                              const std::uint16_t buttons) noexcept {
        auto &operations = *static_cast<Operations *>(value);
        if (operations.set_calls >= operations.buttons.size()) {
            return {false, 0, "unexpected_set_input", {}};
        }
        operations.buttons[operations.set_calls++] = buttons;
        return {true, operations.step_calls, "ok", {}};
    }

    static qa::ReplayFrameOperationResult step(void *const value) noexcept {
        auto &operations = *static_cast<Operations *>(value);
        ++operations.step_calls;
        return {true, operations.step_calls, "ok", {}};
    }
};

} // namespace

int main() {
    try {
        auto inputs = input_source();
        qa::InitialStatePublication publication{complete_fresh_state()};
        require(publication.publish(nullptr, accept), "initial_state_publication_failed");
        qa::RecordingReplayLoop loop{publication, inputs};
        qa::ReplayControlGate controls;
        Operations operations;
        const qa::ReplayFrameOperations callbacks{&operations, Operations::set, Operations::step};

        auto decision = controls.handle({qa::ReplayExternalControl::game_keyboard, 0xffff});
        require(decision.disposition == qa::ReplayControlDisposition::ignored &&
                    decision.code == "game_input_ignored" && inputs.consumed() == 0 &&
                    loop.frames_completed() == 0,
                "manual_game_input_changed_replay");
        decision = controls.handle({qa::ReplayExternalControl::rewind, 0});
        require(decision.disposition == qa::ReplayControlDisposition::ignored &&
                    decision.code == "rewind_ignored" && inputs.consumed() == 0 &&
                    loop.frames_completed() == 0,
                "rewind_changed_replay");
        decision = controls.handle({qa::ReplayExternalControl::fast_forward, 0});
        require(decision.disposition == qa::ReplayControlDisposition::ignored &&
                    decision.code == "fast_forward_ignored" && inputs.consumed() == 0 &&
                    loop.frames_completed() == 0,
                "fast_forward_changed_replay");

        auto frame = loop.execute_next(callbacks);
        require(frame.error == qa::ReplayFrameError::none && operations.buttons[0] == 0x0001 &&
                    operations.set_calls == 1 && operations.step_calls == 1 &&
                    inputs.consumed() == 1 && loop.frames_completed() == 1,
                "first_recorded_input_was_changed");

        decision = controls.handle({qa::ReplayExternalControl::game_keyboard, 0xffff});
        require(decision.disposition == qa::ReplayControlDisposition::ignored &&
                    operations.set_calls == 1 && operations.step_calls == 1 &&
                    inputs.consumed() == 1 && loop.frames_completed() == 1,
                "interleaved_manual_input_changed_replay");

        decision = controls.handle({qa::ReplayExternalControl::cancel, 0});
        require(decision.disposition == qa::ReplayControlDisposition::cancel_requested &&
                    decision.code == "cancel_requested" && controls.cancellation_requested() &&
                    inputs.consumed() == 1 && loop.frames_completed() == 1 &&
                    operations.step_calls == 1,
                "cancellation_was_not_preserved_without_advancing");

        frame = loop.execute_next(callbacks);
        require(frame.error == qa::ReplayFrameError::none && operations.buttons[1] == 0x0002 &&
                    operations.set_calls == 2 && operations.step_calls == 2 &&
                    inputs.consumed() == 2 && loop.frames_completed() == 2 &&
                    controls.cancellation_requested(),
                "recorded_input_or_cancel_request_was_lost");

        std::puts("replay_control_gate_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "replay_control_gate_test: %s\n", error.what());
        return 1;
    }
}
