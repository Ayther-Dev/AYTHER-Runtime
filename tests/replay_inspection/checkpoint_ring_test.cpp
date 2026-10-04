// Spec 002, BR-130 (RF-5.2, RF-5.6, RNF-3; plan §4.4, §5.6, §8 P-12): the checkpoint ring
// keeps a checkpoint every K frames within 512 MiB per take, always keeps the initial
// state, evicts the oldest first, and the planner restores the latest checkpoint at or
// before t−1 and replays silently up to t.
#include "checkpoint_ring.h"
#include "recovery_planner.h"

#include <iostream>
#include <string_view>

namespace ri = ayther::replay_inspection;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

constexpr std::uint64_t mib = 1024U * 1024U;

void budget_and_initial() {
    expect(ri::checkpoint_budget_bytes == 512U * mib, "P-12: 512 MiB of checkpoints per take");
    ri::CheckpointRing ring{60, 100U * mib};
    ring.store_initial(10U * mib);
    expect(ring.should_capture(59) && !ring.should_capture(58) && ring.should_capture(119),
           "plan §5.6: a checkpoint after every K-th frame");
    for (std::uint32_t frame = 59; frame < 60U * 20U; frame += 60)
        expect(ring.store(frame, 10U * mib), "a checkpoint within budget is stored");
    expect(ring.total_bytes() <= 100U * mib, "P-12: the ring never exceeds its budget");
    expect(ring.has_initial() && ring.entries().front().frame == ri::initial_checkpoint_frame,
           "RF-5.2: the initial state is always kept");
    expect(ring.entries()[1].frame == 60 * 12 - 1,
           "RNF-3: the oldest checkpoints are evicted first, never the initial one");
    expect(!ring.store(1199, 200U * mib) && ring.total_bytes() <= 100U * mib,
           "RNF-3: a checkpoint larger than the budget is refused, not truncated");
}

void choice_and_eviction() {
    ri::CheckpointRing ring{60, 512U * mib};
    ring.store_initial(1U * mib);
    for (std::uint32_t frame = 59; frame < 600; frame += 60)
        (void)ring.store(frame, 1U * mib);
    const auto back = ri::plan_recovery(ring, 300, 299);
    expect(back.kind == ri::RecoveryKind::restore && back.checkpoint == 239 &&
               back.replay_from == 240U && back.replay_to == 299U,
           "RF-5.2: the latest checkpoint ≤ t−1 is restored and the rest replayed");
    const auto on_checkpoint = ri::plan_recovery(ring, 300, 240);
    expect(on_checkpoint.checkpoint == 239 && on_checkpoint.replay_from == 240U,
           "RF-5.2: a checkpoint exactly at t−1 replays only t");
    const auto forward = ri::plan_recovery(ring, 300, 301);
    expect(forward.kind == ri::RecoveryKind::step && forward.replay_from == 301U,
           "plan §5.6: the next frame is produced from the current state");
    const auto start = ri::plan_recovery(ring, 30, 0);
    expect(start.kind == ri::RecoveryKind::restore &&
               start.checkpoint == ri::initial_checkpoint_frame && start.replay_from == 0U &&
               start.replay_to == 0U,
           "RF-5.2: frame 0 is reached from the initial state");

    ri::CheckpointRing small{60, 3U * mib};
    small.store_initial(1U * mib);
    for (std::uint32_t frame = 59; frame < 600; frame += 60)
        (void)small.store(frame, 1U * mib);
    const auto evicted = ri::plan_recovery(small, 590, 100);
    expect(evicted.kind == ri::RecoveryKind::restore &&
               evicted.checkpoint == ri::initial_checkpoint_frame && evicted.replay_from == 0U &&
               evicted.replay_to == 100U,
           "RF-5.6: without a checkpoint because of eviction, the initial state is used");
}

} // namespace

int main() {
    budget_and_initial();
    choice_and_eviction();
    if (failures != 0)
        return 1;
    std::cout << "checkpoints and recovery plans follow plan §5.6\n";
    return 0;
}
