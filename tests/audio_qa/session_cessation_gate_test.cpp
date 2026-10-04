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
    return {id,          "session-126", "conditions-126", {"take-main"}, qa::Admission::pending,
            std::nullopt};
}

qa::Run run(const qa::Request &request, const char *const id) {
    qa::Run value;
    value.run_id = id;
    value.request_id = request.request_id;
    value.take_id = request.take_ids.front();
    value.phase = qa::Phase::closed;
    value.playback_result = qa::PlaybackResult::error;
    value.evidence_result = qa::EvidenceResult::incomplete;
    value.last_executed_frame = 44;
    value.last_durable_sample = 35200;
    return value;
}

} // namespace

int main() {
    try {
        qa::SessionOccupancy occupancy;
        const auto active_request = request("request-active");
        const auto next_request = request("request-next");
        require(occupancy.admit(active_request, "run-active").decision ==
                    qa::SessionAdmissionDecision::accepted,
                "active_run_was_not_admitted");

        const auto unconfirmed = run(active_request, "run-active");
        require(occupancy.observe(unconfirmed) && occupancy.occupied(),
                "closed_but_unconfirmed_run_released_session");
        const auto blocked = occupancy.admit(next_request, "run-next");
        require(blocked.decision == qa::SessionAdmissionDecision::busy &&
                    blocked.active_request_id == "request-active" &&
                    blocked.active_run_id == "run-active" && occupancy.queued_requests() == 0,
                "unconfirmed_cessation_did_not_keep_session_busy");

        auto foreign = run(next_request, "run-next");
        foreign.cessation_confirmed = true;
        require(!occupancy.observe(foreign) && occupancy.occupied(),
                "foreign_process_state_released_active_session");

        auto confirmed = unconfirmed;
        confirmed.cessation_confirmed = true;
        require(occupancy.observe(confirmed) && !occupancy.occupied(),
                "matching_confirmed_cessation_did_not_release_session");
        require(occupancy.admit(next_request, "run-next").decision ==
                    qa::SessionAdmissionDecision::accepted,
                "next_run_was_not_admitted_after_confirmed_cessation");

        std::puts("session_cessation_gate_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "session_cessation_gate_test: %s\n", error.what());
        return 1;
    }
}
