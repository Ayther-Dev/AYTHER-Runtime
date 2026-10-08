// Spec 002, BR-191 (RNF-3; plan §8 P-12): measure the checkpoint policy across the
// maximum 54 000-frame take without a GPU, SDL, disk, or wall-clock-dependent assertions.
#include "checkpoint_ring.h"
#include "recovery_planner.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace ri = ayther::replay_inspection;

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

void maximum_take_stays_within_checkpoint_budget() {
    constexpr std::uint32_t frames = 54'000U;
    constexpr std::uint32_t interval = 60U;
    constexpr std::uint64_t periodic_checkpoints = frames / interval;
    constexpr std::uint64_t entries = periodic_checkpoints + 1U; // plus the initial state

    // Put the synthetic take within one division remainder of the approved 512 MiB limit.
    // CheckpointRing accounts serialized core, visual and HD-audio bytes supplied by the
    // real CheckpointStore; this test intentionally measures that domain budget, not RSS.
    constexpr std::uint64_t bytes_per_checkpoint = ri::checkpoint_budget_bytes / entries;
    static_assert(bytes_per_checkpoint > 0U);

    const auto started = std::chrono::steady_clock::now();
    ri::CheckpointRing ring{interval, ri::checkpoint_budget_bytes};
    ring.store_initial(bytes_per_checkpoint);

    std::uint64_t captured{};
    std::uint64_t maximum_accounted_bytes = ring.total_bytes();
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        if (!ring.should_capture(frame))
            continue;
        expect(ring.store(frame, bytes_per_checkpoint),
               "P-12: every periodic checkpoint of the maximum take fits");
        ++captured;
        if (ring.total_bytes() > maximum_accounted_bytes)
            maximum_accounted_bytes = ring.total_bytes();
        expect(ring.total_bytes() <= ri::checkpoint_budget_bytes,
               "P-12: accounted checkpoint bytes never exceed 512 MiB");
    }

    expect(captured == periodic_checkpoints,
           "P-12: a 54 000-frame take captures 900 periodic checkpoints");
    expect(ring.entries().size() == entries && ring.has_initial(),
           "P-12: the initial state and all periodic checkpoints remain available");
    expect(ring.entries().front().frame == ri::initial_checkpoint_frame &&
               ring.entries().back().frame == static_cast<std::int64_t>(frames - 1U),
           "P-12: the retained range covers the initial state through frame 53 999");
    expect(maximum_accounted_bytes == bytes_per_checkpoint * entries &&
               ri::checkpoint_budget_bytes - maximum_accounted_bytes < entries,
           "P-12: the synthetic measurement reaches the approved budget boundary");

    std::uint32_t maximum_replay_frames{};
    for (std::uint32_t target = 0; target < frames; ++target) {
        const auto plan = ri::plan_recovery(ring, frames - 1U, target);
        const auto expected_checkpoint =
            static_cast<std::int64_t>((target / interval) * interval) - 1;
        expect(plan.kind == ri::RecoveryKind::restore && plan.checkpoint == expected_checkpoint &&
                   plan.replay_to == target,
               "P-12: every frame has the nearest deterministic recovery checkpoint");
        const auto replay_frames = plan.replay_to - plan.replay_from + 1U;
        if (replay_frames > maximum_replay_frames)
            maximum_replay_frames = replay_frames;
    }
    expect(maximum_replay_frames == interval,
           "P-12: recovery of the full synthetic take replays at most K frames");

    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started);
    std::cout << "p12 frames=" << frames << " periodic_checkpoints=" << captured
              << " retained_entries=" << ring.entries().size()
              << " bytes_per_checkpoint=" << bytes_per_checkpoint
              << " maximum_accounted_bytes=" << maximum_accounted_bytes
              << " budget_bytes=" << ri::checkpoint_budget_bytes
              << " headroom_bytes=" << (ri::checkpoint_budget_bytes - maximum_accounted_bytes)
              << " maximum_replay_frames=" << maximum_replay_frames
              << " elapsed_us=" << elapsed.count() << '\n';
}

} // namespace

int main() {
    maximum_take_stays_within_checkpoint_budget();
    return failures == 0 ? 0 : 1;
}
