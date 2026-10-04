#include "checkpoint_ring.h"

#include <algorithm>

namespace ayther::replay_inspection {

CheckpointRing::CheckpointRing(std::uint32_t interval, std::uint64_t budget_bytes) noexcept
    : interval_(interval == 0U ? 1U : interval), budget_(budget_bytes) {}

bool CheckpointRing::should_capture(std::uint32_t frame) const noexcept {
    return (frame + 1U) % interval_ == 0U;
}

bool CheckpointRing::has_initial() const noexcept {
    return !entries_.empty() && entries_.front().frame == initial_checkpoint_frame;
}

void CheckpointRing::store_initial(std::uint64_t size_bytes) {
    if (has_initial()) {
        total_ -= entries_.front().size_bytes;
        entries_.front().size_bytes = size_bytes;
    } else {
        entries_.insert(entries_.begin(), {initial_checkpoint_frame, size_bytes});
    }
    total_ += size_bytes;
}

bool CheckpointRing::store(std::uint32_t frame, std::uint64_t size_bytes) {
    const std::uint64_t initial = has_initial() ? entries_.front().size_bytes : 0U;
    if (initial > budget_ || size_bytes > budget_ - initial)
        return false;
    const auto first = has_initial() ? 1U : 0U;
    while (total_ + size_bytes > budget_ && entries_.size() > first) {
        total_ -= entries_[first].size_bytes;
        entries_.erase(entries_.begin() + first);
    }
    const CheckpointEntry entry{static_cast<std::int64_t>(frame), size_bytes};
    const auto position =
        std::upper_bound(entries_.begin(), entries_.end(), entry,
                         [](const CheckpointEntry &left, const CheckpointEntry &right) {
                             return left.frame < right.frame;
                         });
    entries_.insert(position, entry);
    total_ += size_bytes;
    return true;
}

std::optional<std::int64_t> CheckpointRing::latest_at_most(std::int64_t frame) const noexcept {
    std::optional<std::int64_t> found;
    for (const auto &entry : entries_)
        if (entry.frame <= frame)
            found = entry.frame;
    return found;
}

} // namespace ayther::replay_inspection
