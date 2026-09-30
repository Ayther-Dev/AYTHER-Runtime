#include "cancellation_window.h"

namespace ayther::audio_qa {

CancellationWindow::CancellationWindow(const CancellationTrigger trigger,
                                       const std::uint64_t detected_ms) noexcept
    : trigger_(trigger), detected_ms_(detected_ms) {}

bool CancellationWindow::attempt_cancel(const std::uint64_t now_ms,
                                        const bool request_delivered) noexcept {
    if (requested_ms_.has_value() || now_ms < detected_ms_) {
        return false;
    }
    requested_ms_ = now_ms;
    request_delivered_ = request_delivered;
    return true;
}

bool CancellationWindow::observe_response(const std::uint64_t now_ms) noexcept {
    if (!requested_ms_.has_value() || response_ms_.has_value() || now_ms < *requested_ms_) {
        return false;
    }
    response_ms_ = now_ms;
    return true;
}

bool CancellationWindow::confirm_cessation(const std::uint64_t now_ms) noexcept {
    if (!requested_ms_.has_value() || cessation_ms_.has_value() || now_ms < *requested_ms_) {
        return false;
    }
    cessation_ms_ = now_ms;
    return true;
}

std::optional<CancellationDeadlineDiagnostic>
CancellationWindow::check(const std::uint64_t now_ms) const noexcept {
    if (!requested_ms_.has_value() || now_ms < *requested_ms_ || cessation_ms_.has_value()) {
        return {};
    }
    const auto elapsed = now_ms - *requested_ms_;
    std::optional<CancellationDeadlineReason> reason;
    if (elapsed >= cancellation_wait_deadline_ms) {
        reason = CancellationDeadlineReason::wait_timeout;
    } else {
        const bool timely_response =
            response_ms_.has_value() &&
            *response_ms_ - *requested_ms_ <= cancellation_response_deadline_ms;
        if (!timely_response && elapsed >= cancellation_response_deadline_ms) {
            reason = CancellationDeadlineReason::response_timeout;
        }
    }
    if (!reason.has_value()) {
        return {};
    }
    return CancellationDeadlineDiagnostic{trigger_,
                                          *reason,
                                          EvidenceResult::incomplete,
                                          true,
                                          request_delivered_,
                                          response_ms_.has_value(),
                                          false,
                                          detected_ms_,
                                          *requested_ms_,
                                          elapsed};
}

} // namespace ayther::audio_qa
