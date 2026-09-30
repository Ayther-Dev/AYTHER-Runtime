#include "replay_start_policy.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct FrameProbe {
    unsigned ordinary_polls{};
    unsigned recorded_reads{};
    unsigned steps{};
};

void apply(const qa::ReplayStartDecision &decision, FrameProbe &probe) {
    if (decision.consume_ordinary_input) {
        ++probe.ordinary_polls;
    }
    if (decision.consume_recorded_input) {
        ++probe.recorded_reads;
    }
    if (decision.step) {
        ++probe.steps;
    }
}

} // namespace

int main() {
    try {
        qa::RecordingReplayPreparationResult failed{};
        failed.state_error = qa::RecordingStateError::decompression_failed;

        FrameProbe qa_probe;
        const auto qa_failure = qa::decide_replay_start(qa::ReplayLaunchKind::qa_replay, failed);
        apply(qa_failure, qa_probe);
        require(!qa_failure.consume_ordinary_input && !qa_failure.consume_recorded_input &&
                    !qa_failure.step &&
                    qa_failure.diagnostic_code == "qa.game_state_preparation_failed" &&
                    qa_probe.ordinary_polls == 0 && qa_probe.recorded_reads == 0 &&
                    qa_probe.steps == 0,
                "qa_restore_failure_started_free_game");

        FrameProbe ordinary_probe;
        const auto ordinary_failure =
            qa::decide_replay_start(qa::ReplayLaunchKind::ordinary_game, failed);
        apply(ordinary_failure, ordinary_probe);
        require(ordinary_failure.consume_ordinary_input &&
                    !ordinary_failure.consume_recorded_input && ordinary_failure.step &&
                    ordinary_failure.diagnostic_code == "ordinary_restore_fallback" &&
                    ordinary_probe.ordinary_polls == 1 && ordinary_probe.recorded_reads == 0 &&
                    ordinary_probe.steps == 1,
                "ordinary_restore_fallback_changed");

        qa::RecordingReplayPreparationResult rejected{};
        rejected.restore_attempted = true;
        rejected.restore = {false, "bad_format", "core rejected state"};
        const auto rejected_decision =
            qa::decide_replay_start(qa::ReplayLaunchKind::qa_replay, rejected);
        require(!rejected_decision.step &&
                    rejected_decision.diagnostic_code == "qa.game_state_restore_failed" &&
                    rejected.restore.code == "bad_format" &&
                    rejected.restore.detail == "core rejected state",
                "engine_restore_failure_not_preserved");

        std::puts("replay_start_policy_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "replay_start_policy_test: %s\n", error.what());
        return 1;
    }
}
