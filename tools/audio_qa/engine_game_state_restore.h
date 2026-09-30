#pragma once

#include "recording_replay_preparation.h"

namespace ayther::audio_qa {

[[nodiscard]] GameStateRestoreOperationResult
restore_engine_game_state(void *session, const std::vector<std::uint8_t> &state);

} // namespace ayther::audio_qa
