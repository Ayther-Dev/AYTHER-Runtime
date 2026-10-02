#pragma once

#include <cstdint>
#include <string_view>

namespace runtime {

struct MusicRestoreBudgetInput {
  std::uint64_t preparation_elapsed_ms{};
  std::uint64_t transaction_elapsed_ms{};
  std::uint64_t game_state_bytes{};
  std::uint64_t hd_state_bytes{};
  std::uint64_t resource_increment_bytes{};
  bool recoverable{};
};

enum class MusicRestoreBudgetStatus { within_limits, rejected, rolled_back, fatal_closed };

struct MusicRestoreBudgetResult {
  MusicRestoreBudgetStatus status{MusicRestoreBudgetStatus::rejected};
  std::string_view diagnostic;
};

class MusicRestoreBudget {
public:
  [[nodiscard]] MusicRestoreBudgetResult
  evaluate(const MusicRestoreBudgetInput &input) const noexcept {
    constexpr std::uint64_t mib = 1024ULL * 1024ULL;
    if (exceeds_sum(input.game_state_bytes, input.hd_state_bytes, 64 * mib))
      return {MusicRestoreBudgetStatus::rejected, "restore_state_limit"};
    const auto state = input.game_state_bytes + input.hd_state_bytes;
    if (exceeds_sum(state, input.resource_increment_bytes, 128 * mib))
      return {MusicRestoreBudgetStatus::rejected, "restore_memory_limit"};
    if (input.preparation_elapsed_ms > 5'000)
      return timeout(input.recoverable, "restore_preparation_timeout");
    if (input.transaction_elapsed_ms > 2'000)
      return timeout(input.recoverable, "restore_transaction_timeout");
    return {MusicRestoreBudgetStatus::within_limits, {}};
  }

private:
  static constexpr bool exceeds_sum(std::uint64_t left, std::uint64_t right,
                                    std::uint64_t limit) noexcept {
    return left > limit || right > limit - left;
  }
  static constexpr MusicRestoreBudgetResult timeout(bool recoverable,
                                                     std::string_view reason) {
    if (recoverable)
      return {MusicRestoreBudgetStatus::rolled_back, reason};
    return {MusicRestoreBudgetStatus::fatal_closed,
            "restore_failed_state_unknown"};
  }
};

} // namespace runtime
