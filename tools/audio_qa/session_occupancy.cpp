#include "session_occupancy.h"

#include <utility>

namespace ayther::audio_qa {

SessionAdmissionResult SessionOccupancy::admit(const Request &request, std::string run_id) {
    if (request.request_id.empty() || run_id.empty()) {
        return {SessionAdmissionDecision::invalid_request, {}, request_id_, run_id_};
    }
    if (occupied()) {
        return {SessionAdmissionDecision::busy, {}, request_id_, run_id_};
    }
    request_id_ = request.request_id;
    run_id_ = std::move(run_id);
    return {SessionAdmissionDecision::accepted, run_id_, request_id_, run_id_};
}

bool SessionOccupancy::observe(const Run &run) noexcept {
    if (!occupied() || run.request_id != request_id_ || run.run_id != run_id_) {
        return false;
    }
    if (run.phase == Phase::closed && run.cessation_confirmed) {
        request_id_.clear();
        run_id_.clear();
    }
    return true;
}

bool SessionOccupancy::occupied() const noexcept { return !run_id_.empty(); }

std::string_view SessionOccupancy::active_request_id() const noexcept { return request_id_; }

std::string_view SessionOccupancy::active_run_id() const noexcept { return run_id_; }

std::size_t SessionOccupancy::queued_requests() const noexcept { return 0; }

} // namespace ayther::audio_qa
