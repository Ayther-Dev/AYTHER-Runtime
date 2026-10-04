#pragma once

#include "checkpoint_ring.h"

#include <cstdint>

namespace ayther::replay_inspection {

// Spec 002, plan §5.6 (RF-5.2, RF-5.6): how to reach frame t from the confirmed frame.
// The next frame is produced from the current state; any other target restores the
// latest checkpoint at or before t−1 (or the initial state) and replays silently up to t.
enum class RecoveryKind { step, restore, impossible };

struct RecoveryPlan {
    RecoveryKind kind{RecoveryKind::impossible};
    std::int64_t checkpoint{initial_checkpoint_frame};
    std::uint32_t replay_from{};
    std::uint32_t replay_to{};
};

[[nodiscard]] RecoveryPlan plan_recovery(const CheckpointRing &ring, std::uint32_t confirmed,
                                         std::uint32_t target) noexcept;

} // namespace ayther::replay_inspection
