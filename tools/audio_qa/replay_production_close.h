#pragma once

#include "replay_progress.h"

#include <cstdint>
#include <string_view>

namespace ayther::audio_qa {

struct ReplayProductionLimit {
    bool succeeded{};
    bool frozen{};
    bool complete{};
    std::uint64_t last_emulation_frame{};
    std::uint64_t main_sample_limit{};
    std::uint64_t pending_main_frames{};
    std::uint64_t delivered_output_samples_at_freeze{};
    std::uint64_t auxiliary_input_limit{};
    std::string_view code;
    std::string_view detail;
};

struct ReplayVoiceFinalization {
    bool succeeded{};
    bool accepted{};
    bool output_position_known{};
    std::uint64_t finalized_voices{};
    std::uint64_t output_position{};
    std::uint64_t frame_position{};
    std::string_view code;
    std::string_view detail;
};

struct ReplayFrozenDrain {
    bool succeeded{};
    bool accepted{};
    bool complete{};
    std::uint64_t drained_main_frames{};
    std::uint64_t remaining_main_frames{};
    std::uint64_t main_sample_limit{};
    std::string_view code;
    std::string_view detail;
    bool output_complete{};
    std::uint64_t output_sample_limit{};
};

using FreezeReplayProductionOperation = ReplayProductionLimit (*)(void *context) noexcept;
using FinalizeReplayVoicesOperation = ReplayVoiceFinalization (*)(void *context) noexcept;
using DrainReplayProductionOperation = ReplayFrozenDrain (*)(void *context) noexcept;

struct ReplayProductionCloseOperations {
    void *context{};
    FreezeReplayProductionOperation freeze{};
    FinalizeReplayVoicesOperation finalize_voices{};
    DrainReplayProductionOperation drain{};
};

enum class ReplayProductionCloseError {
    none,
    replay_not_finished,
    operations_unavailable,
    freeze_failed,
    invalid_limit,
    finalization_failed,
    drain_failed,
    incomplete_drain,
};

struct ReplayProductionCloseResult {
    ReplayProductionCloseError error{ReplayProductionCloseError::none};
    ReplayProductionLimit limit;
    ReplayVoiceFinalization finalization;
    ReplayFrozenDrain drain;
};

[[nodiscard]] ReplayProductionCloseResult
close_replay_production(const ReplayProgressPublisher &progress,
                        const ReplayProductionCloseOperations &operations) noexcept;

} // namespace ayther::audio_qa
