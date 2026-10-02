#pragma once

#include <cstdint>
#include <string_view>

namespace runtime {

enum class MusicRestoreFailureStage { preparation, application, backend_drain };
enum class MusicRestoreRecoveryStatus { rolled_back, fatal_closed };

struct MusicRestoreRecoveryResult {
  MusicRestoreRecoveryStatus status{MusicRestoreRecoveryStatus::fatal_closed};
  MusicRestoreFailureStage stage{MusicRestoreFailureStage::preparation};
  std::string_view diagnostic;
};

class MusicRestoreRecovery {
public:
  explicit MusicRestoreRecovery(std::uint64_t recoverable_generation) noexcept
      : visible_generation_(recoverable_generation) {}

  [[nodiscard]] MusicRestoreRecoveryResult
  fail(MusicRestoreFailureStage stage, bool rollback_verified) noexcept {
    deliveries_enabled_ = false;
    provisional_pcm_published_ = false;
    if (rollback_verified && visible_generation_ != 0) {
      deliveries_enabled_ = true;
      return {MusicRestoreRecoveryStatus::rolled_back, stage,
              "restore_failed_rolled_back"};
    }
    visible_generation_ = 0;
    session_closed_ = true;
    return {MusicRestoreRecoveryStatus::fatal_closed, stage,
            "restore_failed_state_unknown"};
  }

  [[nodiscard]] std::uint64_t visible_generation() const noexcept {
    return visible_generation_;
  }
  [[nodiscard]] bool session_closed() const noexcept { return session_closed_; }
  [[nodiscard]] bool deliveries_enabled() const noexcept {
    return deliveries_enabled_;
  }
  [[nodiscard]] bool provisional_pcm_published() const noexcept {
    return provisional_pcm_published_;
  }

private:
  std::uint64_t visible_generation_{};
  bool session_closed_{};
  bool deliveries_enabled_{true};
  bool provisional_pcm_published_{};
};

} // namespace runtime
