#include "take_run_sequence.h"

#include <algorithm>
#include <utility>

namespace ayther::audio_qa {
namespace {

bool has_empty_or_duplicate(const std::vector<std::string> &values) {
    for (auto current = values.begin(); current != values.end(); ++current) {
        if (current->empty() ||
            std::find(std::next(current), values.end(), *current) != values.end()) {
            return true;
        }
    }
    return false;
}

} // namespace

TakeRunSequence::TakeRunSequence(Request request, std::vector<std::string> run_ids) {
    if (request.request_id.empty() || request.session_id.empty() || request.conditions_id.empty() ||
        request.take_ids.empty()) {
        error_ = TakeRunSequenceError::invalid_request;
        return;
    }
    if (request.take_ids.size() != run_ids.size()) {
        error_ = TakeRunSequenceError::run_count_mismatch;
        return;
    }
    if (has_empty_or_duplicate(request.take_ids)) {
        error_ = TakeRunSequenceError::duplicate_take;
        return;
    }
    if (has_empty_or_duplicate(run_ids)) {
        error_ = TakeRunSequenceError::duplicate_run;
        return;
    }

    runs_.reserve(request.take_ids.size());
    for (std::size_t index = 0; index < request.take_ids.size(); ++index) {
        Run run;
        run.run_id = std::move(run_ids[index]);
        run.request_id = request.request_id;
        run.take_id = std::move(request.take_ids[index]);
        runs_.push_back(std::move(run));
    }
}

TakeRunSequenceError TakeRunSequence::error() const noexcept { return error_; }

const std::vector<Run> &TakeRunSequence::runs() const noexcept { return runs_; }

bool TakeRunSequence::update(const Run &run) noexcept {
    const auto current = std::find_if(runs_.begin(), runs_.end(), [&](const Run &known) {
        return known.run_id == run.run_id && known.request_id == run.request_id &&
               known.take_id == run.take_id;
    });
    if (current == runs_.end()) {
        return false;
    }
    *current = run;
    return true;
}

} // namespace ayther::audio_qa
