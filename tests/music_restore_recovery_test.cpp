#include "music_restore_recovery.h"

int main() {
  using runtime::MusicRestoreFailureStage;
  using runtime::MusicRestoreRecovery;
  using runtime::MusicRestoreRecoveryStatus;

  for (const auto stage : {MusicRestoreFailureStage::preparation,
                           MusicRestoreFailureStage::application,
                           MusicRestoreFailureStage::backend_drain}) {
    MusicRestoreRecovery recovery{31};
    const auto result = recovery.fail(stage, true);
    if (result.status != MusicRestoreRecoveryStatus::rolled_back ||
        result.diagnostic != "restore_failed_rolled_back" ||
        recovery.visible_generation() != 31 || recovery.session_closed() ||
        recovery.provisional_pcm_published())
      return 1;
  }

  MusicRestoreRecovery fatal{41};
  const auto result = fatal.fail(MusicRestoreFailureStage::application, false);
  if (result.status != MusicRestoreRecoveryStatus::fatal_closed ||
      result.diagnostic != "restore_failed_state_unknown" ||
      !fatal.session_closed() || fatal.deliveries_enabled() ||
      fatal.provisional_pcm_published())
    return 2;
  return 0;
}
