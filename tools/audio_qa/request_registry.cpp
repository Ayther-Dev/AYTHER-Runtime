#include "request_registry.h"

#include <algorithm>
#include <utility>

namespace ayther::audio_qa {
namespace {

bool same_content(const Request &left, const Request &right) noexcept {
    return left.request_id == right.request_id && left.session_id == right.session_id &&
           left.conditions_id == right.conditions_id && left.take_ids == right.take_ids;
}

bool valid(const Request &request, const Run &run) noexcept {
    return !request.request_id.empty() && !request.session_id.empty() &&
           !request.conditions_id.empty() && !request.take_ids.empty() && !run.run_id.empty() &&
           run.request_id == request.request_id && !run.take_id.empty();
}

} // namespace

RequestRegistry::RequestRegistry() { entries_.reserve(max_registered_requests); }

RequestRegistrationResult RequestRegistry::register_request(Request request, Run run) {
    if (!valid(request, run)) {
        return {RequestRegistrationDecision::invalid, {}};
    }
    const auto existing = std::find_if(entries_.begin(), entries_.end(), [&](const Entry &entry) {
        return entry.request.request_id == request.request_id;
    });
    if (existing != entries_.end()) {
        if (!same_content(existing->request, request)) {
            return {RequestRegistrationDecision::identity_conflict, existing->run};
        }
        return {RequestRegistrationDecision::known, existing->run};
    }
    if (entries_.size() >= max_registered_requests) {
        return {RequestRegistrationDecision::capacity_exceeded, {}};
    }
    request.admission = Admission::accepted;
    entries_.push_back({std::move(request), std::move(run)});
    return {RequestRegistrationDecision::accepted, entries_.back().run};
}

bool RequestRegistry::update_run(const Run &run) {
    const auto existing = std::find_if(entries_.begin(), entries_.end(), [&](const Entry &entry) {
        return entry.run.run_id == run.run_id && entry.request.request_id == run.request_id;
    });
    if (existing == entries_.end()) {
        return false;
    }
    existing->run = run;
    return true;
}

std::size_t RequestRegistry::size() const noexcept { return entries_.size(); }

} // namespace ayther::audio_qa
