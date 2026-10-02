#include "music_restore_classification.h"

int main() {
  using runtime::MusicRestoreClassification;
  using runtime::MusicRestoreInput;
  using runtime::MusicRestoreOutcome;

  MusicRestoreClassification classifier;
  MusicRestoreInput input;
  input.game_valid = true;
  input.core_compatible = true;
  input.pack_required = true;
  input.pack_valid = true;
  input.session_active = true;

  for (const auto missing : {0, 1, 2}) {
    auto fresh = input;
    fresh.hd_present = missing != 0;
    fresh.hd_complete = missing != 1;
    fresh.hd_compatible = missing != 2;
    const auto result = classifier.classify(fresh);
    if (result.outcome != MusicRestoreOutcome::fresh_hd ||
        !result.original_audio || !result.session_preserved ||
        result.rollback_required)
      return 1;
  }

  auto invalid_game = input;
  invalid_game.game_valid = false;
  auto invalid_pack = input;
  invalid_pack.pack_valid = false;
  for (const auto rejected : {invalid_game, invalid_pack}) {
    const auto result = classifier.classify(rejected);
    if (result.outcome != MusicRestoreOutcome::rejected ||
        !result.session_preserved || result.original_audio)
      return 2;
  }

  auto operational = input;
  operational.hd_present = operational.hd_complete =
      operational.hd_compatible = true;
  operational.apply_failed = true;
  const auto failed = classifier.classify(operational);
  if (failed.outcome != MusicRestoreOutcome::rollback ||
      !failed.rollback_required || failed.original_audio ||
      failed.diagnostic != "restore_apply_failed")
    return 3;
  return 0;
}
