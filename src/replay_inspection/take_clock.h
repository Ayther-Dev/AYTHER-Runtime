#pragma once

#include <cstdint>
#include <optional>

namespace ayther::replay_inspection {

// Spec 002, plan §5.8 (RF-3.1, RF-7.1, RF-7.10): the only conversion between take frames
// and times. T = 1 / timing_fps and N is the number of frames of the take. Preparation,
// pause and navigation never add time to the take.
class TakeClock final {
  public:
    struct Times {
        // Absent before the first frame.
        std::optional<std::uint32_t> frame;
        double elapsed_ms{};
        double remaining_ms{};
    };

    TakeClock(std::uint32_t frames, double timing_fps) noexcept;

    [[nodiscard]] std::uint32_t frames() const noexcept { return frames_; }
    [[nodiscard]] double period_ms() const noexcept { return period_ms_; }

    // No current frame: elapsed 0, remaining N·T.
    [[nodiscard]] Times before_first() const noexcept;
    // Frame k ∈ 0..N−1: elapsed (k+1)·T, remaining N·T − elapsed; no frame outside the take.
    [[nodiscard]] Times at(std::uint32_t frame) const noexcept;
    // The Engine increments its frame index before producing: take frame = index − 1.
    [[nodiscard]] std::optional<std::uint32_t>
    take_frame_from_engine(std::uint64_t frame_index) const noexcept;

  private:
    std::uint32_t frames_;
    double period_ms_;
};

} // namespace ayther::replay_inspection
