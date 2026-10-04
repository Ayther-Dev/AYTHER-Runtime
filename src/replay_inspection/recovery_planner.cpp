#include "recovery_planner.h"

namespace ayther::replay_inspection {

RecoveryPlan plan_recovery(const CheckpointRing &ring, std::uint32_t confirmed,
                           std::uint32_t target) noexcept {
    if (target == confirmed + 1U)
        return {RecoveryKind::step, static_cast<std::int64_t>(confirmed), target, target};
    const auto checkpoint = ring.latest_at_most(static_cast<std::int64_t>(target) - 1);
    if (!checkpoint)
        return {};
    return {RecoveryKind::restore, *checkpoint, static_cast<std::uint32_t>(*checkpoint + 1),
            target};
}

} // namespace ayther::replay_inspection
