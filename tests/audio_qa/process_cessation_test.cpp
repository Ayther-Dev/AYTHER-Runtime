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

qa::Run last_known_run() {
    qa::Run run;
    run.run_id = "run-117";
    run.request_id = "request-117";
    run.take_id = "take-main";
    run.phase = qa::Phase::playing;
    run.playback_result = qa::PlaybackResult::in_progress;
    run.evidence_result = qa::EvidenceResult::in_progress;
    run.last_executed_frame = 120;
    run.last_durable_sample = 96000;
    return run;
}

} // namespace

int main() {
    try {
        const auto snapshot = last_known_run();
        qa::ProcessCessation cessation{snapshot};
        require(cessation.observe_disconnect() && cessation.request_stop() &&
                    cessation.mark_unconfirmed(),
                "unconfirmed_cessation_sequence_failed");

        const auto &unconfirmed = cessation.status();
        require(unconfirmed.state == qa::ProcessCessationState::unconfirmed &&
                    unconfirmed.last_known_run == snapshot &&
                    !unconfirmed.last_known_run.cessation_confirmed &&
                    unconfirmed.last_known_run.phase == qa::Phase::playing &&
                    unconfirmed.last_known_run.playback_result == qa::PlaybackResult::in_progress &&
                    !unconfirmed.exit_code.has_value(),
                "disconnect_was_presented_as_termination");

        auto invented = snapshot;
        invented.phase = qa::Phase::closed;
        invented.playback_result = qa::PlaybackResult::error;
        require(!cessation.observe_run(invented) && cessation.status().last_known_run == snapshot,
                "post_disconnect_state_replaced_last_confirmed_snapshot");

        require(cessation.confirm_exit(-1) &&
                    cessation.status().state == qa::ProcessCessationState::confirmed &&
                    cessation.status().last_known_run.cessation_confirmed &&
                    cessation.status().last_known_run.phase == qa::Phase::playing &&
                    cessation.status().exit_code == -1,
                "later_exit_confirmation_was_not_distinguished");

        std::puts("process_cessation_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "process_cessation_test: %s\n", error.what());
        return 1;
    }
}
