#pragma once

#include "recording_replay_preparation.h"

#include <string_view>

namespace ayther::audio_qa {

enum class ReplayLaunchKind { ordinary_game, qa_replay };

struct ReplayStartDecision {
    bool consume_ordinary_input{};
    bool consume_recorded_input{};
    bool step{};
    std::string_view diagnostic_code;
};

[[nodiscard]] ReplayStartDecision
decide_replay_start(ReplayLaunchKind launch,
                    const RecordingReplayPreparationResult &preparation) noexcept;

} // namespace ayther::audio_qa
