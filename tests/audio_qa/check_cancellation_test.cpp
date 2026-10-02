#include "check_cancellation.h"
#include "console_interrupt.h"

#include <csignal>
#include <span>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

struct Capture {
    bool accept{true};
    std::size_t calls{};
    std::vector<std::byte> bytes;

    static bool send(void *context, const std::span<const std::byte> payload) noexcept {
        auto &capture = *static_cast<Capture *>(context);
        ++capture.calls;
        capture.bytes.assign(payload.begin(), payload.end());
        return capture.accept;
    }
};

bool signal_is_captured() {
    qa::ControlledInterrupt interrupt;
    {
        qa::ConsoleInterruptHandler handler{interrupt};
        if (!handler.installed() || std::raise(SIGINT) != 0 || !interrupt.requested())
            return false;
    }
    return true;
}

bool confirmed_flow() {
    Capture capture;
    qa::CheckCancellationCoordinator coordinator{"request-a", "run-a", 900};
    if (!coordinator.request(1000, {&capture, Capture::send}) ||
        coordinator.request(1001, {&capture, Capture::send}) || capture.calls != 1)
        return false;
    const auto decoded =
        qa::decode_cancellation_message(capture.bytes, qa::CancellationStage::requested, 1);
    const auto *request = std::get_if<qa::CancellationMessage>(&decoded);
    if (request == nullptr || request->request_id != "request-a" || request->run_id != "run-a")
        return false;

    const qa::CancellationMessage wrong{
        "request-b", "run-a", qa::CancellationStage::acknowledged, 0, {}};
    const qa::CancellationMessage acknowledged{
        "request-a", "run-a", qa::CancellationStage::acknowledged, 0, {}};
    const qa::CancellationMessage applied{"request-a", "run-a", qa::CancellationStage::applied, 4,
                                          99};
    const qa::CancellationMessage ceased{"request-a", "run-a",
                                         qa::CancellationStage::cessation_confirmed, 4, 99};
    return !coordinator.observe(wrong, 1100) && coordinator.observe(acknowledged, 1200) &&
           coordinator.observe(applied, 1300) && coordinator.observe(ceased, 1400) &&
           coordinator.status().state == qa::CheckCancellationState::cessation_confirmed &&
           coordinator.status().request_delivered && coordinator.status().response_received &&
           coordinator.status().cessation_confirmed &&
           coordinator.poll(10000).state == qa::CheckCancellationState::cessation_confirmed;
}

bool missing_response_and_cessation_are_distinct() {
    Capture capture;
    capture.accept = false;
    qa::CheckCancellationCoordinator coordinator{"request-b", "run-b", 900};
    if (!coordinator.request(1000, {&capture, Capture::send}) ||
        coordinator.status().request_delivered ||
        coordinator.poll(2999).state != qa::CheckCancellationState::requested ||
        coordinator.poll(3000).state != qa::CheckCancellationState::response_missing)
        return false;
    const auto terminal = coordinator.poll(6000);
    return terminal.state == qa::CheckCancellationState::cessation_unknown &&
           !terminal.response_received && !terminal.cessation_confirmed;
}

bool response_does_not_imply_cessation() {
    Capture capture;
    qa::CheckCancellationCoordinator coordinator{"request-c", "run-c", 900};
    const qa::CancellationMessage acknowledged{
        "request-c", "run-c", qa::CancellationStage::acknowledged, 0, {}};
    if (!coordinator.request(1000, {&capture, Capture::send}) ||
        !coordinator.observe(acknowledged, 1500) || coordinator.poll(5999).cessation_confirmed)
        return false;
    const auto terminal = coordinator.poll(6000);
    return terminal.state == qa::CheckCancellationState::cessation_unknown &&
           terminal.response_received && !terminal.cessation_confirmed;
}

} // namespace

int main() {
    return signal_is_captured() && confirmed_flow() &&
                   missing_response_and_cessation_are_distinct() &&
                   response_does_not_imply_cessation()
               ? 0
               : 1;
}
