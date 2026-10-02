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
        qa::ProgressWatchdog stalled{qa::WatchdogPhase::playback, 1000};
        require(stalled.observe_completed_frame(7, 2000) && stalled.observe_heartbeat(4000) &&
                    stalled.observe_completed_frame(7, 5000) &&
                    stalled.observe_completed_frame(6, 6000) && stalled.observe_heartbeat(6999) &&
                    !stalled.check(6999).has_value(),
                "heartbeat_or_nonadvancing_frame_changed_progress_boundary");
        const auto no_progress = stalled.check(7000);
        require(no_progress.has_value() &&
                    no_progress->reason == qa::WatchdogTimeoutReason::no_progress &&
                    no_progress->evidence_result == qa::EvidenceResult::incomplete &&
                    no_progress->last_completed_frame == std::uint32_t{7} &&
                    no_progress->last_progress_ms == 2000 && no_progress->elapsed_total_ms == 6000,
                "playback_no_progress_timeout_was_not_reported");

        qa::ProgressWatchdog resumed{qa::WatchdogPhase::playback, 10000};
        require(resumed.observe_completed_frame(1, 11000) && resumed.observe_heartbeat(15999) &&
                    !resumed.check(15999).has_value() &&
                    resumed.observe_completed_frame(2, 16000) && resumed.observe_heartbeat(20999) &&
                    !resumed.check(20999).has_value(),
                "advancing_frame_did_not_reset_progress_boundary");
        const auto resumed_timeout = resumed.check(21000);
        require(resumed_timeout.has_value() &&
                    resumed_timeout->reason == qa::WatchdogTimeoutReason::no_progress &&
                    resumed_timeout->last_completed_frame == std::uint32_t{2} &&
                    resumed_timeout->last_progress_ms == 16000,
                "resumed_playback_timeout_was_not_reported");

        std::puts("playback_watchdog_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "playback_watchdog_test: %s\n", error.what());
        return 1;
    }
}
