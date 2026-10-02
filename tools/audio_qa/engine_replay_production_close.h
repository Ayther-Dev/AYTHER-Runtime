#pragma once

#include "replay_production_close.h"

namespace ayther::audio_qa {

[[nodiscard]] ReplayProductionCloseOperations
engine_replay_production_close_operations(void *session) noexcept;

} // namespace ayther::audio_qa
