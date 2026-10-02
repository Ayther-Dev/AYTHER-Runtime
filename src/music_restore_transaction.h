#pragma once

#include <cstdint>

namespace runtime {

class MusicRestoreTransaction {
  public:
    MusicRestoreTransaction(std::uint64_t generation, std::uint64_t boundary, bool host_paused,
                            bool game_music_paused) noexcept
        : visible_generation_(generation), visible_boundary_(boundary), host_paused_(host_paused),
          game_music_paused_(game_music_paused) {}

    [[nodiscard]] bool prepare(std::uint64_t generation, std::uint64_t boundary,
                               bool destination_game_music_running) noexcept {
        if (prepared_ || published_ || generation <= visible_generation_ ||
            boundary <= visible_boundary_)
            return false;
        prepared_ = true;
        prepared_generation_ = generation;
        prepared_boundary_ = boundary;
        prepared_game_music_paused_ = !destination_game_music_running;
        return true;
    }

    [[nodiscard]] bool publish(std::uint64_t boundary) noexcept {
        if (!prepared_ || published_ || boundary != prepared_boundary_)
            return false;
        visible_generation_ = prepared_generation_;
        visible_boundary_ = prepared_boundary_;
        game_music_paused_ = prepared_game_music_paused_;
        link_active_ = false;
        published_ = true;
        states_visible_together_ = true;
        return true;
    }

    void set_link_active(bool active) noexcept { link_active_ = active; }
    [[nodiscard]] bool accepts_decision(std::uint64_t generation) const noexcept {
        return published_ && generation == visible_generation_;
    }
    [[nodiscard]] bool accepts_pcm(std::uint64_t generation, std::uint64_t frame) const noexcept {
        return published_ && generation == visible_generation_ && frame >= visible_boundary_;
    }
    [[nodiscard]] std::uint64_t visible_generation() const noexcept { return visible_generation_; }
    [[nodiscard]] bool host_paused() const noexcept { return host_paused_; }
    [[nodiscard]] bool game_music_paused() const noexcept { return game_music_paused_; }
    [[nodiscard]] bool states_visible_together() const noexcept { return states_visible_together_; }
    [[nodiscard]] bool link_active() const noexcept { return link_active_; }

  private:
    std::uint64_t visible_generation_{};
    std::uint64_t visible_boundary_{};
    std::uint64_t prepared_generation_{};
    std::uint64_t prepared_boundary_{};
    bool host_paused_{};
    bool game_music_paused_{};
    bool prepared_game_music_paused_{};
    bool prepared_{};
    bool published_{};
    bool states_visible_together_{};
    bool link_active_{};
};

} // namespace runtime
