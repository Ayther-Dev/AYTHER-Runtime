#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace runtime {

struct MusicPcmOutput {
    std::uint64_t generation{};
    std::uint64_t first_frame{};
    std::vector<float> samples;
};

class MusicPcmBackend {
  public:
    [[nodiscard]] bool enqueue(std::uint64_t generation, std::span<const float> samples) {
        if (generation == 0 || samples.empty() ||
            (active_generation_ != 0 && generation != active_generation_) || deliveries_stopped_)
            return false;
        active_generation_ = generation;
        queue_.insert(queue_.end(), samples.begin(), samples.end());
        return true;
    }

    [[nodiscard]] bool stop_deliveries(std::uint64_t request_frame) noexcept {
        if (deliveries_stopped_ || request_frame == 0)
            return false;
        deliveries_stopped_ = true;
        request_frame_ = request_frame;
        return true;
    }

    [[nodiscard]] bool invalidate_and_drain(std::uint64_t generation) noexcept {
        if (!deliveries_stopped_ || generation != active_generation_)
            return false;
        discarded_frames_ += queue_.size();
        queue_.clear();
        drain_performed_ = true;
        return true;
    }

    [[nodiscard]] bool confirm_drained(std::uint64_t confirmation_frame) noexcept {
        if (!deliveries_stopped_ || !drain_performed_ || !queue_.empty() ||
            confirmation_frame < request_frame_)
            return false;
        confirmed_ = true;
        confirmation_frame_ = confirmation_frame;
        return true;
    }

    [[nodiscard]] bool publish_generation(std::uint64_t generation,
                                          std::uint64_t boundary) noexcept {
        if (!confirmed_ || generation <= active_generation_ || boundary < confirmation_frame_)
            return false;
        active_generation_ = generation;
        first_output_frame_ = boundary;
        deliveries_stopped_ = false;
        confirmed_ = false;
        drain_performed_ = false;
        return true;
    }

    [[nodiscard]] bool rollback(std::uint64_t generation) noexcept {
        if (!deliveries_stopped_ || generation != active_generation_)
            return false;
        deliveries_stopped_ = false;
        confirmed_ = false;
        drain_performed_ = false;
        return true;
    }

    [[nodiscard]] MusicPcmOutput consume() {
        if (deliveries_stopped_)
            return {};
        MusicPcmOutput output{active_generation_, first_output_frame_, std::move(queue_)};
        queue_.clear();
        return output;
    }

    [[nodiscard]] std::size_t queued_frames() const noexcept { return queue_.size(); }
    [[nodiscard]] std::uint64_t active_generation() const noexcept { return active_generation_; }

  private:
    std::vector<float> queue_;
    std::uint64_t active_generation_{};
    std::uint64_t request_frame_{};
    std::uint64_t confirmation_frame_{};
    std::uint64_t first_output_frame_{};
    std::uint64_t discarded_frames_{};
    bool deliveries_stopped_{};
    bool drain_performed_{};
    bool confirmed_{};
};

} // namespace runtime
