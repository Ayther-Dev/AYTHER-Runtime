#include "check_cancellation.h"

#include <utility>
#include <variant>

namespace ayther::audio_qa {

CheckCancellationCoordinator::CheckCancellationCoordinator(std::string request_id,
                                                           std::string run_id,
                                                           const std::uint64_t detected_ms)
    : request_id_(std::move(request_id)), run_id_(std::move(run_id)),
      window_(CancellationTrigger::controlled_interrupt, detected_ms) {}

bool CheckCancellationCoordinator::request(const std::uint64_t now_ms,
                                           const CancellationSender sender) noexcept {
    if (status_.state != CheckCancellationState::idle)
        return false;
    const CancellationMessage message{request_id_, run_id_, CancellationStage::requested, 0, {}};
    const auto encoded = encode_cancellation_message(message, 1);
    const auto *bytes = std::get_if<std::vector<std::byte>>(&encoded);
    const bool delivered =
        bytes != nullptr && sender.send != nullptr && sender.send(sender.context, *bytes);
    if (!window_.attempt_cancel(now_ms, delivered))
        return false;
    status_.state = CheckCancellationState::requested;
    status_.request_delivered = delivered;
    return true;
}

bool CheckCancellationCoordinator::observe(const CancellationMessage &message,
                                           const std::uint64_t now_ms) noexcept {
    if (message.request_id != request_id_ || message.run_id != run_id_ ||
        status_.state == CheckCancellationState::idle || status_.cessation_confirmed)
        return false;

    switch (message.stage) {
    case CancellationStage::requested:
        return false;
    case CancellationStage::acknowledged:
        if (!window_.observe_response(now_ms))
            return false;
        status_.state = CheckCancellationState::acknowledged;
        status_.response_received = true;
        return true;
    case CancellationStage::applied:
        if (!status_.response_received && !window_.observe_response(now_ms))
            return false;
        status_.state = CheckCancellationState::applied;
        status_.response_received = true;
        return true;
    case CancellationStage::cessation_confirmed:
        if (!window_.confirm_cessation(now_ms))
            return false;
        status_.state = CheckCancellationState::cessation_confirmed;
        status_.cessation_confirmed = true;
        return true;
    }
    return false;
}

CheckCancellationStatus CheckCancellationCoordinator::poll(const std::uint64_t now_ms) noexcept {
    const auto diagnostic = window_.check(now_ms);
    if (!diagnostic.has_value())
        return status_;
    status_.state = diagnostic->reason == CancellationDeadlineReason::response_timeout
                        ? CheckCancellationState::response_missing
                        : CheckCancellationState::cessation_unknown;
    status_.response_received = diagnostic->response_received;
    status_.cessation_confirmed = false;
    return status_;
}

const CheckCancellationStatus &CheckCancellationCoordinator::status() const noexcept {
    return status_;
}

} // namespace ayther::audio_qa
