#pragma once

#include <cstddef>
#include <cstdint>

namespace runtime {

class MusicDiscontinuityState {
public:
  [[nodiscard]] bool begin_take(std::uint64_t take_id) noexcept {
    if (take_id == 0 || take_id == take_id_)
      return false;
    take_id_ = take_id;
    entry_count_ = 0;
    discontinuity_recorded_ = false;
    active_music_ = candidate_count_ = pending_count_ = 0;
    original_audio_ = false;
    exact_entry_count_claimed_ = true;
    return true;
  }

  [[nodiscard]] bool observe_pattern(std::uint64_t begin,
                                     std::uint64_t end) noexcept {
    if (take_id_ == 0 || begin >= end)
      return false;
    ++entry_count_;
    last_pattern_end_ = end;
    return true;
  }

  [[nodiscard]] bool navigate(std::uint64_t destination) noexcept {
    if (take_id_ == 0 || destination == last_pattern_end_)
      return false;
    discontinuity_recorded_ = true;
    navigation_destination_ = destination;
    return true;
  }

  [[nodiscard]] bool link_across_jump(std::uint64_t source,
                                      std::uint64_t destination) const noexcept {
    return !discontinuity_recorded_ && source == last_pattern_end_ &&
           destination != navigation_destination_;
  }

  void seed_active_music(std::size_t active, std::size_t candidates,
                         std::size_t pending) noexcept {
    active_music_ = active;
    candidate_count_ = candidates;
    pending_count_ = pending;
  }

  [[nodiscard]] bool load_legacy_save() noexcept {
    active_music_ = candidate_count_ = pending_count_ = 0;
    original_audio_ = true;
    discontinuity_recorded_ = true;
    return true;
  }

  void close_partial() noexcept {
    exact_entry_count_claimed_ = false;
    synthetic_close_frames_ = 0;
  }

  [[nodiscard]] std::uint64_t take_id() const noexcept { return take_id_; }
  [[nodiscard]] std::size_t entry_count() const noexcept { return entry_count_; }
  [[nodiscard]] bool discontinuity_recorded() const noexcept {
    return discontinuity_recorded_;
  }
  [[nodiscard]] std::size_t active_music() const noexcept { return active_music_; }
  [[nodiscard]] std::size_t candidate_count() const noexcept { return candidate_count_; }
  [[nodiscard]] std::size_t pending_count() const noexcept { return pending_count_; }
  [[nodiscard]] bool original_audio() const noexcept { return original_audio_; }
  [[nodiscard]] bool exact_entry_count_claimed() const noexcept {
    return exact_entry_count_claimed_;
  }
  [[nodiscard]] std::uint64_t synthetic_close_frames() const noexcept {
    return synthetic_close_frames_;
  }

private:
  std::uint64_t take_id_{};
  std::uint64_t last_pattern_end_{};
  std::uint64_t navigation_destination_{};
  std::size_t entry_count_{};
  std::size_t active_music_{};
  std::size_t candidate_count_{};
  std::size_t pending_count_{};
  std::uint64_t synthetic_close_frames_{};
  bool discontinuity_recorded_{};
  bool original_audio_{};
  bool exact_entry_count_claimed_{true};
};

} // namespace runtime
