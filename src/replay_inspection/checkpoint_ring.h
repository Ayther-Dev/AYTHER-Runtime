#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace ayther::replay_inspection {

// Spec 002, plan §4.4, §5.6 and §8 P-12 (RF-5.2, RF-5.6, RNF-3): the checkpoints of a take.
// A checkpoint holds the state after producing its frame; the initial state is frame −1.
inline constexpr std::int64_t initial_checkpoint_frame = -1;
inline constexpr std::uint64_t checkpoint_budget_bytes = 512ULL * 1024ULL * 1024ULL;

struct CheckpointEntry {
    std::int64_t frame{};
    std::uint64_t size_bytes{};
};

class CheckpointRing final {
  public:
    CheckpointRing(std::uint32_t interval, std::uint64_t budget_bytes) noexcept;

    // A checkpoint is taken after every K-th frame: K−1, 2K−1, …
    [[nodiscard]] bool should_capture(std::uint32_t frame) const noexcept;
    // The initial state is always kept and never evicted.
    void store_initial(std::uint64_t size_bytes);
    // Evicts the oldest checkpoints, never the initial one, until it fits. A checkpoint that
    // can never fit is refused.
    [[nodiscard]] bool store(std::uint32_t frame, std::uint64_t size_bytes);

    [[nodiscard]] std::optional<std::int64_t> latest_at_most(std::int64_t frame) const noexcept;
    [[nodiscard]] bool has_initial() const noexcept;
    [[nodiscard]] const std::vector<CheckpointEntry> &entries() const noexcept { return entries_; }
    [[nodiscard]] std::uint64_t total_bytes() const noexcept { return total_; }

  private:
    std::uint32_t interval_;
    std::uint64_t budget_;
    std::uint64_t total_{};
    std::vector<CheckpointEntry> entries_;
};

} // namespace ayther::replay_inspection
