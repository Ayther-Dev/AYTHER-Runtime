#pragma once

#include <cstdint>

namespace runtime {

class MusicBackendDrain {
  public:
    [[nodiscard]] bool request(std::uint64_t frame, std::uint64_t generation) noexcept {
        if (requested_ || frame == 0 || generation == 0)
            return false;
        requested_ = true;
        request_frame_ = frame;
        old_generation_ = generation;
        return true;
    }

    void record_emitted(std::uint64_t frames) noexcept {
        if (requested_ && !published_)
            emitted_irreversible_frames_ += frames;
    }
    void record_discarded(std::uint64_t frames) noexcept {
        if (requested_ && !published_) {
            discarded_frames_ += frames;
            discard_traced_ = discard_traced_ || frames != 0;
        }
    }

    [[nodiscard]] bool confirm(std::uint64_t confirmation_frame,
                               std::uint64_t boundary_frame) noexcept {
        if (!requested_ || confirmed_ || confirmation_frame < request_frame_ ||
            boundary_frame < confirmation_frame)
            return false;
        confirmed_ = true;
        confirmation_frame_ = confirmation_frame;
        boundary_frame_ = boundary_frame;
        return true;
    }

    [[nodiscard]] bool publish(std::uint64_t generation) noexcept {
        if (!confirmed_ || published_ || generation <= old_generation_)
            return false;
        published_ = true;
        new_generation_ = generation;
        return true;
    }

    [[nodiscard]] std::uint64_t request_frame() const noexcept { return request_frame_; }
    [[nodiscard]] std::uint64_t confirmation_frame() const noexcept { return confirmation_frame_; }
    [[nodiscard]] std::uint64_t boundary_frame() const noexcept { return boundary_frame_; }
    [[nodiscard]] std::uint64_t old_generation() const noexcept { return old_generation_; }
    [[nodiscard]] std::uint64_t new_generation() const noexcept { return new_generation_; }
    [[nodiscard]] std::uint64_t emitted_irreversible_frames() const noexcept {
        return emitted_irreversible_frames_;
    }
    [[nodiscard]] std::uint64_t discarded_frames() const noexcept { return discarded_frames_; }
    [[nodiscard]] bool discard_traced() const noexcept { return discard_traced_; }

  private:
    bool requested_{};
    bool confirmed_{};
    bool published_{};
    bool discard_traced_{};
    std::uint64_t request_frame_{};
    std::uint64_t confirmation_frame_{};
    std::uint64_t boundary_frame_{};
    std::uint64_t old_generation_{};
    std::uint64_t new_generation_{};
    std::uint64_t emitted_irreversible_frames_{};
    std::uint64_t discarded_frames_{};
};

} // namespace runtime
