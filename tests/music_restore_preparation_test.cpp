#include "music_restore_preparation.h"

#include <cstdio>

int main() {
  using runtime::MusicRestorePreparation;
  using runtime::MusicRestorePreparationInput;
  using runtime::MusicRestorePreparationStatus;
  constexpr std::uint64_t mib = 1024ULL * 1024ULL;

  MusicRestorePreparation preparation;
  MusicRestorePreparationInput limit;
  limit.game_valid = limit.core_compatible = limit.pack_valid = true;
  limit.assets_valid = limit.schema_valid = limit.ranges_valid = true;
  limit.elapsed_ms = 5'000;
  limit.game_state_bytes = 40 * mib;
  limit.hd_state_bytes = 24 * mib;
  limit.resource_increment_bytes = 64 * mib;
  if (preparation.prepare(limit).status != MusicRestorePreparationStatus::prepared ||
      preparation.published())
    return 1;

  auto over_time = limit;
  over_time.elapsed_ms = 5'001;
  auto over_state = limit;
  ++over_state.hd_state_bytes;
  auto over_total = limit;
  ++over_total.resource_increment_bytes;
  if (preparation.prepare(over_time).diagnostic != "restore_preparation_timeout" ||
      preparation.prepare(over_state).diagnostic != "restore_state_limit" ||
      preparation.prepare(over_total).diagnostic != "restore_memory_limit")
    return 2;

  for (int field = 0; field < 6; ++field) {
    auto invalid = limit;
    bool *flags[] = {&invalid.game_valid, &invalid.core_compatible,
                     &invalid.pack_valid, &invalid.assets_valid,
                     &invalid.schema_valid, &invalid.ranges_valid};
    *flags[field] = false;
    if (preparation.prepare(invalid).status !=
            MusicRestorePreparationStatus::rejected ||
        preparation.published())
      return 3;
  }
  if (preparation.prepare_from_audio_thread(limit).status !=
          MusicRestorePreparationStatus::rejected)
    return 4;
  return 0;
}
