#include "progress_watchdog.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        qa::ProgressWatchdog stalled{qa::WatchdogPhase::closing, 1000};
        require(stalled.observe_bytes_received(100, 2000) &&
                    stalled.observe_bytes_durable(40, 2000) &&
                    stalled.observe_bytes_received(200, 6000) &&
                    stalled.observe_bytes_received(300, 11999) && !stalled.check(11999).has_value(),
                "received_bytes_changed_durable_progress_boundary");
        const auto no_progress = stalled.check(12000);
        require(no_progress.has_value() &&
                    no_progress->reason == qa::WatchdogTimeoutReason::no_progress &&
                    no_progress->evidence_result == qa::EvidenceResult::incomplete &&
                    no_progress->bytes_received == 300 && no_progress->bytes_durable == 40 &&
                    no_progress->last_progress_ms == 2000,
                "closing_no_progress_timeout_was_not_reported");

        qa::ProgressWatchdog total{qa::WatchdogPhase::closing, 10000};
        require(total.observe_bytes_received(100, 11000) &&
                    total.observe_bytes_durable(100, 11000) &&
                    total.observe_bytes_received(200, 19000) &&
                    total.observe_bytes_durable(200, 19000) &&
                    total.observe_bytes_received(300, 28000) &&
                    total.observe_bytes_durable(300, 28000) &&
                    total.observe_bytes_received(400, 39000) &&
                    total.observe_bytes_durable(400, 39000) && !total.check(39999).has_value(),
                "closing_total_boundary_fired_early");
        require(!total.check(40000).has_value(),
                "progressing_large_capture_was_rejected_by_fixed_total_timeout");
        const auto stalled_after_progress = total.check(49000);
        require(stalled_after_progress.has_value() &&
                    stalled_after_progress->reason == qa::WatchdogTimeoutReason::no_progress &&
                    stalled_after_progress->evidence_result == qa::EvidenceResult::incomplete &&
                    stalled_after_progress->bytes_received == 400 &&
                    stalled_after_progress->bytes_durable == 400 &&
                    stalled_after_progress->last_progress_ms == 39000,
                "large_capture_no_progress_timeout_was_not_reported");

        qa::ProgressWatchdog invalid{qa::WatchdogPhase::closing, 0};
        require(invalid.observe_bytes_received(10, 1) && !invalid.observe_bytes_durable(11, 2) &&
                    !invalid.observe_bytes_received(9, 2),
                "invalid_closing_counters_were_accepted");

        // Campaign 2026-10-07: in an inspection the end of the captured PCM can move back
        // while closing (1878240 -> 1862400). That is still progress, not a corrupt counter:
        // the close watchdog observes a monotonic progress of the position.
        {
            qa::MonotonicProgress progress;
            qa::ProgressWatchdog closing{qa::WatchdogPhase::closing, 0};
            const auto first = progress.observe(1878240);
            const auto back = progress.observe(1862400);
            const auto forward = progress.observe(1870560);
            require(first == 0U && back == 15840U && forward == 23999U + 1U &&
                        closing.observe_bytes_received(first, 1) &&
                        closing.observe_bytes_durable(first, 1) &&
                        closing.observe_bytes_received(back, 7) &&
                        closing.observe_bytes_durable(back, 7) &&
                        closing.observe_bytes_received(forward, 20) &&
                        closing.observe_bytes_durable(forward, 20) &&
                        !closing.check(20).has_value(),
                    "a PCM end that moves back while closing was taken as a failure");
            const auto still = progress.observe(1870560);
            require(still == forward && closing.observe_bytes_received(still, 9000) &&
                        closing.observe_bytes_durable(still, 9000) &&
                        closing.check(10020).has_value(),
                    "a PCM end that stops moving did not time out");
        }

        std::puts("closing_watchdog_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "closing_watchdog_test: %s\n", error.what());
        return 1;
    }
}
