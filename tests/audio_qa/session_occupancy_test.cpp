#include "run_lifecycle.h"
#include "session_occupancy.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

qa::Request request(const char *const id) {
    return {id, "session-110", "conditions-110", {"take-main"}, qa::Admission::pending};
}

qa::Run run(const qa::Request &request, const char *const id) {
    qa::Run value;
    value.run_id = id;
    value.request_id = request.request_id;
    value.take_id = request.take_ids.front();
    return value;
}

void require_busy(const qa::SessionAdmissionResult &result, const qa::SessionOccupancy &occupancy,
                  const char *const message) {
    require(result.decision == qa::SessionAdmissionDecision::busy &&
                result.active_request_id == "request-a" && result.active_run_id == "run-a" &&
                occupancy.occupied() && occupancy.active_request_id() == "request-a" &&
                occupancy.active_run_id() == "run-a" && occupancy.queued_requests() == 0,
            message);
}

} // namespace

int main() {
    try {
        qa::SessionOccupancy occupancy;
        const auto first_request = request("request-a");
        const auto second_request = request("request-b");
        const auto admitted = occupancy.admit(first_request, "run-a");
        require(admitted.decision == qa::SessionAdmissionDecision::accepted &&
                    admitted.run_id == "run-a" && occupancy.occupied(),
                "first_request_was_not_admitted");

        qa::RunLifecycle lifecycle{run(first_request, "run-a")};
        require(occupancy.observe(lifecycle.run()), "preparing_run_was_not_observed");
        require_busy(occupancy.admit(second_request, "run-b"), occupancy,
                     "preparing_session_did_not_return_busy");

        require(lifecycle.mark_ready() == qa::RunTransitionError::none &&
                    lifecycle.start_playback() == qa::RunTransitionError::none &&
                    occupancy.observe(lifecycle.run()),
                "playing_transition_failed");
        require_busy(occupancy.admit(second_request, "run-b"), occupancy,
                     "playing_session_did_not_return_busy");

        require(lifecycle.begin_closing(qa::PlaybackResult::natural_end) ==
                        qa::RunTransitionError::none &&
                    occupancy.observe(lifecycle.run()),
                "closing_transition_failed");
        require_busy(occupancy.admit(second_request, "run-b"), occupancy,
                     "closing_session_did_not_return_busy");

        require(lifecycle.finish_closing(qa::EvidenceResult::complete) ==
                    qa::RunTransitionError::none,
                "closing_completion_failed");
        auto closed = lifecycle.run();
        closed.cessation_confirmed = true;
        require(occupancy.observe(closed) && !occupancy.occupied(),
                "closed_run_did_not_release_session");
        const auto next = occupancy.admit(second_request, "run-b");
        require(next.decision == qa::SessionAdmissionDecision::accepted && next.run_id == "run-b" &&
                    occupancy.queued_requests() == 0,
                "released_session_did_not_accept_next_request");

        std::puts("session_occupancy_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "session_occupancy_test: %s\n", error.what());
        return 1;
    }
}
