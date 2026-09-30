#include "initial_state_publication.h"
#include "recording_replay_loop.h"
#include "replay_production_close.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <string_view>
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
            {qa::Availability::known, qa::FactId{"run-107", "initial-state", 1}, {}}};
}

qa::InitialState complete_fresh_state() {
    qa::InitialState state;
    state.run_id = "run-107";
    state.game_state_id = "game-state-107";
    state.game_restore_result = qa::RestoreResult::succeeded;
    state.hd_initialization = qa::HdInitialization::fresh;
    state.initialization_reason = "hd_state_absent";
    state.detector = {qa::Availability::known, qa::DetectorInitial{true, 0, 0, {}}, {}};
    state.detector_observed_by = {
        qa::Availability::known, qa::FactId{"run-107", "initial-state", 1}, {}};
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
void discard_progress(void *, const qa::ReplayProgressEvent &) noexcept {}

struct Fixture {
    std::uint32_t set_calls{};
    std::uint32_t step_calls{};
    std::array<std::string_view, 3> close_order{};
    std::size_t close_calls{};

    static qa::ReplayFrameOperationResult set(void *const value, const std::uint32_t,
                                              const std::uint16_t) noexcept {
        auto &fixture = *static_cast<Fixture *>(value);
        ++fixture.set_calls;
        return {true, fixture.step_calls, "ok", {}};
    }

    static qa::ReplayFrameOperationResult step(void *const value) noexcept {
        auto &fixture = *static_cast<Fixture *>(value);
        ++fixture.step_calls;
        return {true, 1000U + fixture.step_calls, "ok", {}};
    }

    static qa::ReplayProductionLimit freeze(void *const value) noexcept {
        auto &fixture = *static_cast<Fixture *>(value);
        fixture.close_order[fixture.close_calls++] = "freeze";
        return {true, true, true, 1002, 4096, 4, 4080, 512, "ok", {}};
    }

    static qa::ReplayVoiceFinalization finalize(void *const value) noexcept {
        auto &fixture = *static_cast<Fixture *>(value);
        fixture.close_order[fixture.close_calls++] = "finalize";
        return {true, true, true, 2, 4096, 1002, "ok", {}};
    }

    static qa::ReplayFrozenDrain drain(void *const value) noexcept {
        auto &fixture = *static_cast<Fixture *>(value);
        fixture.close_order[fixture.close_calls++] = "drain";
        return {true, true, true, 4, 0, 4096, "ok", {}, true, 4096};
    }
};

} // namespace

int main() {
    try {
        auto inputs = input_source();
        qa::InitialStatePublication publication{complete_fresh_state()};
        require(publication.publish(nullptr, accept), "initial_state_publication_failed");
        qa::ReplayProgressPublisher progress{inputs.frame_count(), nullptr, discard_progress};
        require(progress.publish_acceptance(), "acceptance_publication_failed");
        qa::RecordingReplayLoop loop{publication, inputs, &progress};
        Fixture fixture;
        const qa::ReplayFrameOperations frames{&fixture, Fixture::set, Fixture::step};
        require(loop.execute_next(frames).error == qa::ReplayFrameError::none &&
                    loop.execute_next(frames).error == qa::ReplayFrameError::none &&
                    loop.execute_next(frames).error == qa::ReplayFrameError::input_exhausted &&
                    progress.finished(),
                "replay_did_not_finish_at_n");

        const qa::ReplayProductionCloseOperations close_operations{
            &fixture, Fixture::freeze, Fixture::finalize, Fixture::drain};
        const auto closed = qa::close_replay_production(progress, close_operations);
        require(
            closed.error == qa::ReplayProductionCloseError::none && fixture.close_calls == 3 &&
                fixture.close_order ==
                    std::array<std::string_view, 3>{"freeze", "finalize", "drain"} &&
                closed.limit.last_emulation_frame == 1002 &&
                closed.limit.main_sample_limit == 4096 && closed.limit.pending_main_frames == 4 &&
                closed.drain.drained_main_frames == 4 && closed.drain.remaining_main_frames == 0 &&
                closed.drain.output_complete && closed.drain.output_sample_limit == 4096 &&
                fixture.set_calls == 2 && fixture.step_calls == 2,
            "production_close_did_not_preserve_exact_frozen_boundary");

        auto unfinished_inputs = input_source();
        qa::ReplayProgressPublisher unfinished{unfinished_inputs.frame_count(), nullptr,
                                               discard_progress};
        require(unfinished.publish_acceptance(), "unfinished_acceptance_publication_failed");
        const auto rejected = qa::close_replay_production(unfinished, close_operations);
        require(rejected.error == qa::ReplayProductionCloseError::replay_not_finished &&
                    fixture.close_calls == 3,
                "unfinished_replay_reached_engine_close_operations");

        std::puts("replay_production_close_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "replay_production_close_test: %s\n", error.what());
        return 1;
    }
}
