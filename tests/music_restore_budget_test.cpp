#include "music_restore_budget.h"

int main() {
  using runtime::MusicRestoreBudget;
  using runtime::MusicRestoreBudgetInput;
  using runtime::MusicRestoreBudgetStatus;
  constexpr std::uint64_t mib = 1024ULL * 1024ULL;

  MusicRestoreBudget budget;
  MusicRestoreBudgetInput edge{5'000, 2'000, 40 * mib, 24 * mib, 64 * mib, true};
  if (budget.evaluate(edge).status != MusicRestoreBudgetStatus::within_limits)
    return 1;

  auto preparation = edge;
  preparation.preparation_elapsed_ms++;
  auto transaction = edge;
  transaction.transaction_elapsed_ms++;
  auto state = edge;
  state.hd_state_bytes++;
  auto memory = edge;
  memory.resource_increment_bytes++;
  if (budget.evaluate(preparation).status != MusicRestoreBudgetStatus::rolled_back ||
      budget.evaluate(transaction).status != MusicRestoreBudgetStatus::rolled_back ||
      budget.evaluate(state).diagnostic != "restore_state_limit" ||
      budget.evaluate(memory).diagnostic != "restore_memory_limit")
    return 2;

  transaction.recoverable = false;
  const auto fatal = budget.evaluate(transaction);
  if (fatal.status != MusicRestoreBudgetStatus::fatal_closed ||
      fatal.diagnostic != "restore_failed_state_unknown")
    return 3;
  return 0;
}
