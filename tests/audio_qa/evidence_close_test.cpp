#include "evidence_close.h"
#include "replay_terminal_arbiter.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

qa::RunLifecycle playing() {
    qa::Run run;
    run.run_id = "run-116";
    run.request_id = "request-116";
    run.take_id = "take-main";
    qa::RunLifecycle lifecycle{run};
    if (lifecycle.mark_ready() != qa::RunTransitionError::none ||
        lifecycle.start_playback() != qa::RunTransitionError::none) {
        throw std::runtime_error("lifecycle_fixture_failed");
    }
    return lifecycle;
}

} // namespace

int main() {
    try {
        auto lifecycle = playing();
        qa::ReplayTerminalArbiter terminal;
        const qa::ReplayTerminalEvent cancelled{qa::PlaybackResult::cancelled, 1, std::uint32_t{0},
                                                std::uint64_t{801}};
        require(terminal.resolve(cancelled, lifecycle) == qa::ReplayTerminalDecision::accepted &&
                    lifecycle.run().phase == qa::Phase::closing,
                "cancellation_terminal_was_not_fixed");

        qa::EvidenceClose close;
        const qa::EvidenceCloseReport failed{qa::EvidenceCloseFailure::durable_write_failed,
                                             std::uint64_t{4096}};
        require(close.finish(failed, lifecycle) && close.report() == failed &&
                    lifecycle.run().phase == qa::Phase::closed &&
                    lifecycle.run().playback_result == qa::PlaybackResult::cancelled &&
                    lifecycle.run().evidence_result == qa::EvidenceResult::incomplete,
                "close_failure_rewrote_or_hid_cancellation");

        const qa::ReplayTerminalEvent late_error{qa::PlaybackResult::error, 1, std::uint32_t{0},
                                                 std::uint64_t{801}};
        require(terminal.resolve(late_error, lifecycle) ==
                        qa::ReplayTerminalDecision::already_resolved &&
                    lifecycle.run().playback_result == qa::PlaybackResult::cancelled &&
                    lifecycle.run().evidence_result == qa::EvidenceResult::incomplete,
                "late_close_error_rewrote_terminal_result");

        std::puts("evidence_close_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "evidence_close_test: %s\n", error.what());
        return 1;
    }
}
