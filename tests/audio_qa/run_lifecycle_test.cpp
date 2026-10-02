#include "run_lifecycle.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

qa::Run pending_run() {
    qa::Run run;
    run.run_id = "run-108";
    run.request_id = "request-108";
    run.take_id = "take-main";
    return run;
}

} // namespace

int main() {
    try {
        qa::RunLifecycle lifecycle{pending_run()};
        require(lifecycle.error() == qa::RunTransitionError::none &&
                    lifecycle.run().phase == qa::Phase::preparing &&
                    lifecycle.run().playback_result == qa::PlaybackResult::not_started,
                "preparation_phase_was_not_preserved");
        require(lifecycle.start_playback() == qa::RunTransitionError::invalid_transition &&
                    lifecycle.run().phase == qa::Phase::preparing,
                "playback_skipped_readiness");
        require(lifecycle.mark_ready() == qa::RunTransitionError::none &&
                    lifecycle.run().phase == qa::Phase::ready &&
                    lifecycle.run().playback_result == qa::PlaybackResult::not_started,
                "readiness_was_confused_with_playback");
        require(lifecycle.start_playback() == qa::RunTransitionError::none &&
                    lifecycle.run().phase == qa::Phase::playing &&
                    lifecycle.run().playback_result == qa::PlaybackResult::in_progress,
                "playback_phase_did_not_start");
        require(lifecycle.begin_closing(qa::PlaybackResult::in_progress) ==
                        qa::RunTransitionError::invalid_terminal_result &&
                    lifecycle.run().phase == qa::Phase::playing,
                "non_terminal_result_started_closing");
        require(lifecycle.begin_closing(qa::PlaybackResult::natural_end) ==
                        qa::RunTransitionError::none &&
                    lifecycle.run().phase == qa::Phase::closing &&
                    lifecycle.run().playback_result == qa::PlaybackResult::natural_end,
                "natural_end_did_not_enter_closing");
        require(lifecycle.finish_closing(qa::EvidenceResult::complete) ==
                        qa::RunTransitionError::none &&
                    lifecycle.run().phase == qa::Phase::closed &&
                    lifecycle.run().playback_result == qa::PlaybackResult::natural_end &&
                    lifecycle.run().evidence_result == qa::EvidenceResult::complete,
                "closing_did_not_reach_terminal_phase");
        require(lifecycle.finish_closing(qa::EvidenceResult::complete) ==
                        qa::RunTransitionError::invalid_transition &&
                    lifecycle.run().phase == qa::Phase::closed,
                "terminal_phase_was_not_stable");

        auto invalid_run = pending_run();
        invalid_run.phase = qa::Phase::ready;
        const qa::RunLifecycle invalid{invalid_run};
        require(invalid.error() == qa::RunTransitionError::invalid_initial_state,
                "invalid_initial_phase_was_accepted");

        std::puts("run_lifecycle_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "run_lifecycle_test: %s\n", error.what());
        return 1;
    }
}
