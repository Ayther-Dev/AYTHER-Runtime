#include "take_clock.h"

#include <cmath>

namespace ayther::replay_inspection {

TakeClock::TakeClock(std::uint32_t frames, double timing_fps) noexcept
    : frames_(frames),
      period_ms_(std::isfinite(timing_fps) && timing_fps > 0.0 ? 1000.0 / timing_fps : 0.0) {}

TakeClock::Times TakeClock::before_first() const noexcept {
    return {std::nullopt, 0.0, static_cast<double>(frames_) * period_ms_};
}

TakeClock::Times TakeClock::at(std::uint32_t frame) const noexcept {
    if (frame >= frames_)
        return before_first();
    const double elapsed = static_cast<double>(frame + 1U) * period_ms_;
    const double total = static_cast<double>(frames_) * period_ms_;
    return {frame, elapsed, frame + 1U == frames_ ? 0.0 : total - elapsed};
}

std::optional<std::uint32_t>
TakeClock::take_frame_from_engine(std::uint64_t frame_index) const noexcept {
    if (frame_index == 0U || frame_index > frames_)
        return std::nullopt;
    return static_cast<std::uint32_t>(frame_index - 1U);
}

} // namespace ayther::replay_inspection
