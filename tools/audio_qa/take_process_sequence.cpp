#include "take_process_sequence.h"

#include <algorithm>
#include <utility>

namespace ayther::audio_qa {

TakeProcessSequence::TakeProcessSequence(std::vector<Run> runs) : runs_(std::move(runs)) {
    process_instance_ids_.reserve(runs_.size());
}

std::optional<TakeProcessStart> TakeProcessSequence::start_next(std::string process_instance_id) {
    if (active() || current_index_ >= runs_.size() || process_instance_id.empty() ||
        std::find(process_instance_ids_.begin(), process_instance_ids_.end(),
                  process_instance_id) != process_instance_ids_.end()) {
        return {};
    }
    process_instance_ids_.push_back(process_instance_id);
    active_process_instance_id_ = process_instance_id;
    return TakeProcessStart{current_index_, runs_[current_index_], std::move(process_instance_id)};
}

bool TakeProcessSequence::finish_current(const Run &run) noexcept {
    if (!active() || current_index_ >= runs_.size()) {
        return false;
    }
    const auto &expected = runs_[current_index_];
    if (run.run_id != expected.run_id || run.request_id != expected.request_id ||
        run.take_id != expected.take_id || run.phase != Phase::closed || !run.cessation_confirmed) {
        return false;
    }
    runs_[current_index_] = run;
    ++current_index_;
    active_process_instance_id_.reset();
    return true;
}

bool TakeProcessSequence::active() const noexcept {
    return active_process_instance_id_.has_value();
}

std::size_t TakeProcessSequence::completed_count() const noexcept { return current_index_; }

const std::vector<Run> &TakeProcessSequence::runs() const noexcept { return runs_; }

} // namespace ayther::audio_qa
