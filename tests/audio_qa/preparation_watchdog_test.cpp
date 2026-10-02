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
        qa::ProgressWatchdog stalled{qa::WatchdogPhase::preparation, 10000};
        require(stalled.observe_preparation_stage("materials_verified", 20000) &&
                    stalled.observe_heartbeat(70000) &&
                    stalled.observe_preparation_stage("materials_verified", 90000) &&
                    stalled.observe_heartbeat(139999) && !stalled.check(139999).has_value(),
                "heartbeat_or_repeated_stage_changed_progress_boundary");
        const auto no_progress = stalled.check(140000);
        require(no_progress.has_value() &&
                    no_progress->reason == qa::WatchdogTimeoutReason::no_progress &&
                    no_progress->evidence_result == qa::EvidenceResult::incomplete &&
                    no_progress->last_preparation_stage == "materials_verified" &&
                    no_progress->last_progress_ms == 20000,
                "preparation_no_progress_timeout_was_not_reported");

        qa::ProgressWatchdog total{qa::WatchdogPhase::preparation, 10000};
        require(total.observe_preparation_stage("reference_checked", 100000) &&
                    total.observe_preparation_stage("pack_loaded", 250000) &&
                    total.observe_preparation_stage("state_restored", 309999) &&
                    !total.check(309999).has_value(),
                "preparation_total_boundary_fired_early");
        const auto total_timeout = total.check(310000);
        require(total_timeout.has_value() &&
                    total_timeout->reason == qa::WatchdogTimeoutReason::total_time &&
                    total_timeout->evidence_result == qa::EvidenceResult::incomplete &&
                    total_timeout->last_preparation_stage == "state_restored" &&
                    total_timeout->elapsed_total_ms == 300000,
                "preparation_total_timeout_was_not_reported");

        std::puts("preparation_watchdog_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "preparation_watchdog_test: %s\n", error.what());
        return 1;
    }
}
