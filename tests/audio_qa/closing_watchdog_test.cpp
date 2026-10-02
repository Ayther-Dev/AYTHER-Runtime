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

        std::puts("closing_watchdog_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "closing_watchdog_test: %s\n", error.what());
        return 1;
    }
}
