#include "cancellation_window.h"
#include "process_cessation.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

qa::Run active_run() {
    qa::Run run;
    run.run_id = "run-125";
    run.request_id = "request-125";
    run.take_id = "take-main";
    run.phase = qa::Phase::playing;
    run.playback_result = qa::PlaybackResult::in_progress;
    run.evidence_result = qa::EvidenceResult::in_progress;
    run.last_executed_frame = 44;
    run.last_durable_sample = 35200;
    return run;
}

} // namespace

int main() {
    try {
        const auto snapshot = active_run();
        qa::ProcessCessation cessation{snapshot};
        qa::CancellationWindow lost{qa::CancellationTrigger::channel_lost, 1000};
        require(cessation.observe_disconnect() && lost.attempt_cancel(1010, false) &&
                    cessation.request_stop() && !lost.check(3009).has_value(),
                "channel_loss_cancel_attempt_or_early_boundary_failed");

        const auto response_timeout = lost.check(3010);
        require(response_timeout.has_value() &&
                    response_timeout->reason == qa::CancellationDeadlineReason::response_timeout &&
                    response_timeout->evidence_result == qa::EvidenceResult::incomplete &&
                    response_timeout->cancellation_attempted &&
                    !response_timeout->request_delivered && !response_timeout->response_received &&
                    !response_timeout->cessation_confirmed &&
                    response_timeout->requested_ms == 1010 &&
                    response_timeout->elapsed_since_request_ms == 2000,
                "missing_response_was_not_reported_at_two_seconds");

        const auto before_wait_timeout = lost.check(6009);
        require(before_wait_timeout.has_value() &&
                    before_wait_timeout->reason == qa::CancellationDeadlineReason::response_timeout,
                "five_second_wait_was_added_after_response_deadline");
        const auto wait_timeout = lost.check(6010);
        require(wait_timeout.has_value() &&
                    wait_timeout->reason == qa::CancellationDeadlineReason::wait_timeout &&
                    wait_timeout->elapsed_since_request_ms == 5000 &&
                    !wait_timeout->cessation_confirmed && cessation.mark_unconfirmed() &&
                    cessation.status().state == qa::ProcessCessationState::unconfirmed &&
                    cessation.status().last_known_run == snapshot,
                "unconfirmed_cessation_was_not_bounded_at_five_seconds");

        qa::CancellationWindow bounded{qa::CancellationTrigger::no_progress, 10000};
        require(bounded.attempt_cancel(11000, true) && bounded.observe_response(13000) &&
                    !bounded.check(15999).has_value() && bounded.confirm_cessation(16000) &&
                    !bounded.check(16000).has_value(),
                "inclusive_deadlines_rejected_timely_response_or_cessation");

        std::puts("cancellation_window_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "cancellation_window_test: %s\n", error.what());
        return 1;
    }
}
