#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

namespace runtime {

inline constexpr std::uint64_t restore_preparation_limit_ms = 5'000;
inline constexpr std::uint64_t restore_state_limit_bytes = 64ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t restore_increment_limit_bytes = 128ULL * 1024ULL * 1024ULL;

struct MusicRestorePreparationInput {
  bool game_valid{};
  bool core_compatible{};
  bool pack_valid{};
  bool assets_valid{};
  bool schema_valid{};
  bool ranges_valid{};
  std::uint64_t elapsed_ms{};
  std::uint64_t game_state_bytes{};
  std::uint64_t hd_state_bytes{};
  std::uint64_t resource_increment_bytes{};
};

enum class MusicRestorePreparationStatus { prepared, rejected };

struct MusicRestorePreparationResult {
  MusicRestorePreparationStatus status{MusicRestorePreparationStatus::rejected};
  std::string_view diagnostic;
};

class MusicRestorePreparation {
public:
  [[nodiscard]] MusicRestorePreparationResult
  prepare(const MusicRestorePreparationInput &input) const noexcept {
    if (!input.game_valid || !input.core_compatible)
      return rejected("restore_game_incompatible");
    if (!input.pack_valid || !input.assets_valid || !input.schema_valid ||
        !input.ranges_valid)
      return rejected("restore_hd_invalid");
    if (input.elapsed_ms > restore_preparation_limit_ms)
      return rejected("restore_preparation_timeout");
    if (sum_exceeds(input.game_state_bytes, input.hd_state_bytes,
                    restore_state_limit_bytes))
      return rejected("restore_state_limit");
    const auto state_bytes = input.game_state_bytes + input.hd_state_bytes;
    if (sum_exceeds(state_bytes, input.resource_increment_bytes,
                    restore_increment_limit_bytes))
      return rejected("restore_memory_limit");
    return {MusicRestorePreparationStatus::prepared, {}};
  }

  [[nodiscard]] MusicRestorePreparationResult
  prepare_from_audio_thread(const MusicRestorePreparationInput &) const noexcept {
    return rejected("restore_preparation_on_audio_thread");
  }

  [[nodiscard]] constexpr bool published() const noexcept { return false; }

private:
  static constexpr MusicRestorePreparationResult rejected(std::string_view reason) {
    return {MusicRestorePreparationStatus::rejected, reason};
  }
  static constexpr bool sum_exceeds(std::uint64_t left, std::uint64_t right,
                                    std::uint64_t limit) noexcept {
    return left > limit || right > limit - left;
  }
};

} // namespace runtime
