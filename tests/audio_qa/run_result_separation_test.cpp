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

qa::RunLifecycle playing_run() {
    qa::Run run;
    run.run_id = "run-109";
    run.request_id = "request-109";
    run.take_id = "take-main";
    qa::RunLifecycle lifecycle{run};
    if (lifecycle.mark_ready() != qa::RunTransitionError::none ||
        lifecycle.start_playback() != qa::RunTransitionError::none) {
        throw std::runtime_error("playing_fixture_failed");
    }
    return lifecycle;
}

} // namespace

int main() {
    try {
        auto failed_write = playing_run();
        require(failed_write.begin_closing(qa::PlaybackResult::natural_end) ==
                        qa::RunTransitionError::none &&
                    failed_write.finish_closing(qa::EvidenceResult::incomplete) ==
                        qa::RunTransitionError::none &&
                    failed_write.run().phase == qa::Phase::closed &&
                    failed_write.run().playback_result == qa::PlaybackResult::natural_end &&
                    failed_write.run().evidence_result == qa::EvidenceResult::incomplete,
                "write_failure_replaced_natural_playback_result");

        auto pending_write = playing_run();
        require(pending_write.begin_closing(qa::PlaybackResult::natural_end) ==
                        qa::RunTransitionError::none &&
                    pending_write.finish_closing(qa::EvidenceResult::pending) ==
                        qa::RunTransitionError::invalid_evidence_result &&
                    pending_write.run().phase == qa::Phase::closing &&
                    pending_write.run().playback_result == qa::PlaybackResult::natural_end &&
                    pending_write.run().evidence_result == qa::EvidenceResult::pending,
                "pending_evidence_was_published_as_terminal");

        std::puts("run_result_separation_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "run_result_separation_test: %s\n", error.what());
        return 1;
    }
}
