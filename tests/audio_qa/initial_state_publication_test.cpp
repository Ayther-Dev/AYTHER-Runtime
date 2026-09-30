#include "initial_state_publication.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <utility>
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
            {qa::Availability::known, qa::FactId{"run-102", "initial-state", 1}, {}}};
}

qa::InitialState complete_fresh_state() {
    qa::InitialState state;
    state.run_id = "run-102";
    state.game_state_id = "game-state-102";
    state.game_restore_result = qa::RestoreResult::succeeded;
    state.hd_initialization = qa::HdInitialization::fresh;
    state.initialization_reason = "hd_state_absent";
    state.detector = {qa::Availability::known, qa::DetectorInitial{true, 0, 0, {}}, {}};
    state.detector_observed_by = {
        qa::Availability::known, qa::FactId{"run-102", "initial-state", 1}, {}};
    state.windows = empty_collection<qa::InitialWindow>();
    state.voices = empty_collection<qa::Occurrence>();
    state.requests = empty_collection<qa::InitialRequest>();
    state.pending_audio = empty_collection<qa::PendingAudio>();
    return state;
}

qa::RecordingInputSource input_source() {
    static constexpr std::array bytes{std::byte{0x34}, std::byte{0x12}, std::byte{0xcd},
                                      std::byte{0xab}};
    qa::RecordingLayout layout;
    layout.frame_count = 2;
    layout.inputs = {0, bytes.size()};
    auto result = qa::make_recording_input_source(bytes, layout);
    if (result.error != qa::RecordingInputSourceError::none) {
        throw std::runtime_error("input_fixture_rejected");
    }
    return result.source;
}

struct Consumer {
    std::array<std::string_view, 3> order{};
    std::size_t order_size{};
    unsigned calls{};
    bool accept{true};
    bool complete{};

    static bool receive(void *const value, const qa::InitialState &state) noexcept {
        auto &consumer = *static_cast<Consumer *>(value);
        ++consumer.calls;
        consumer.order[consumer.order_size++] = "initial_state";
        consumer.complete = qa::has_complete_hd_description(state);
        return consumer.accept;
    }
};

} // namespace

int main() {
    try {
        auto inputs = input_source();
        qa::InitialStatePublication publication{complete_fresh_state()};
        require(!publication.next_input(inputs).has_value() && inputs.consumed() == 0 &&
                    !publication.input_permitted(),
                "input_zero_consumed_before_initial_state");

        Consumer consumer;
        require(publication.publish(&consumer, Consumer::receive) &&
                    publication.status() == qa::InitialStatePublicationStatus::acknowledged &&
                    publication.error() == qa::InitialStatePublicationError::none &&
                    publication.input_permitted() && consumer.calls == 1 && consumer.complete,
                "complete_initial_state_not_acknowledged");
        require(publication.publish(&consumer, Consumer::receive) && consumer.calls == 1,
                "acknowledged_initial_state_published_twice");

        const auto first = publication.next_input(inputs);
        consumer.order[consumer.order_size++] = "input_0";
        consumer.order[consumer.order_size++] = "first_frame_fact";
        require(first.has_value() && first->frame == 0 && first->buttons == 0x1234 &&
                    inputs.consumed() == 1 && consumer.order_size == 3 &&
                    consumer.order == std::array<std::string_view, 3>{"initial_state", "input_0",
                                                                      "first_frame_fact"},
                "initial_state_not_ordered_before_first_frame");

        auto rejected_inputs = input_source();
        Consumer rejected_consumer;
        rejected_consumer.accept = false;
        qa::InitialStatePublication rejected{complete_fresh_state()};
        require(!rejected.publish(&rejected_consumer, Consumer::receive) &&
                    rejected.error() == qa::InitialStatePublicationError::consumer_rejected &&
                    !rejected.next_input(rejected_inputs).has_value() &&
                    rejected_inputs.consumed() == 0 &&
                    !rejected.publish(&rejected_consumer, Consumer::receive) &&
                    rejected_consumer.calls == 1,
                "consumer_rejection_released_or_republished_input");

        auto incomplete_state = complete_fresh_state();
        incomplete_state.detector.value->reset_verified = false;
        qa::InitialStatePublication incomplete{std::move(incomplete_state)};
        auto incomplete_inputs = input_source();
        Consumer incomplete_consumer;
        require(!incomplete.publish(&incomplete_consumer, Consumer::receive) &&
                    incomplete.error() == qa::InitialStatePublicationError::incomplete_state &&
                    incomplete_consumer.calls == 0 &&
                    !incomplete.next_input(incomplete_inputs).has_value() &&
                    incomplete_inputs.consumed() == 0,
                "incomplete_initial_state_reached_consumer_or_input");

        std::puts("initial_state_publication_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "initial_state_publication_test: %s\n", error.what());
        return 1;
    }
}
