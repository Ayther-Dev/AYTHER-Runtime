#pragma once

#include "recording_replay_loop.h"

namespace ayther::audio_qa {

[[nodiscard]] ReplayFrameOperations engine_recording_replay_operations(void *session) noexcept;

} // namespace ayther::audio_qa
