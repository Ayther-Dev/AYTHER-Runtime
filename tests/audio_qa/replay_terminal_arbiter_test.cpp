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

qa::RunLifecycle playing(const char *const run_id) {
    qa::Run run;
    run.run_id = run_id;
    run.request_id = "request-115";
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
        auto natural_first = playing("run-natural-first");
        qa::ReplayTerminalArbiter natural_arbiter;
        const qa::ReplayTerminalEvent natural{qa::PlaybackResult::natural_end, 2, std::uint32_t{1},
                                              std::uint64_t{702}};
        const qa::ReplayTerminalEvent late_cancel{qa::PlaybackResult::cancelled, 2,
                                                  std::uint32_t{1}, std::uint64_t{702}};
        require(natural_arbiter.resolve(natural, natural_first) ==
                        qa::ReplayTerminalDecision::accepted &&
                    natural_arbiter.resolve(late_cancel, natural_first) ==
                        qa::ReplayTerminalDecision::already_resolved &&
                    natural_arbiter.terminal() == natural &&
                    natural_first.run().playback_result == qa::PlaybackResult::natural_end,
                "late_cancellation_rewrote_natural_end");

        auto cancel_first = playing("run-cancel-first");
        qa::ReplayTerminalArbiter cancel_arbiter;
        const qa::ReplayTerminalEvent cancellation{qa::PlaybackResult::cancelled, 1,
                                                   std::uint32_t{0}, std::uint64_t{701}};
        const qa::ReplayTerminalEvent late_finish{qa::PlaybackResult::natural_end, 2,
                                                  std::uint32_t{1}, std::uint64_t{702}};
        require(cancel_arbiter.resolve(cancellation, cancel_first) ==
                        qa::ReplayTerminalDecision::accepted &&
                    cancel_arbiter.resolve(late_finish, cancel_first) ==
                        qa::ReplayTerminalDecision::already_resolved &&
                    cancel_arbiter.terminal() == cancellation &&
                    cancel_first.run().playback_result == qa::PlaybackResult::cancelled,
                "late_natural_end_rewrote_cancellation");

        std::puts("replay_terminal_arbiter_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "replay_terminal_arbiter_test: %s\n", error.what());
        return 1;
    }
}
