#include "runtime_music_snapshot.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using namespace ayther::engine;

bool complete_state_is_captured_at_one_boundary() {
    runtime::MusicHdSnapshotState hd;
    hd.generation = 7;
    hd.recognition = {10, 930'000, true};
    hd.identity = MusicIdentityId{1};
    hd.occurrence = OccurrenceId{2};
    hd.node = SequenceNodeId{3};
    hd.segment = SequenceSegmentId{4};
    hd.appearance = AppearanceId{5};
    hd.visit = 6;
    hd.iteration = 8;
    hd.bus_owner = AudioBusId{9};
    hd.voices = {{11, 1200, 64, 7}};
    hd.queue = {13, SequenceNodeId{14}, 7, AppearanceId{5}};
    hd.envelopes = {{15, 32, 96}};
    hd.pending = {{16, 7, 21}};
    hd.resource_revisions = {{AssetId{17}, 4}};

    runtime::MusicSnapshotCoordinator coordinator;
    const std::array<std::uint8_t, 4> game{1, 2, 3, 4};
    const auto snapshot = coordinator.capture(240, game, hd);
    return snapshot && snapshot->boundary == 240 &&
           snapshot->game_state == std::vector<std::uint8_t>(game.begin(), game.end()) &&
           snapshot->hd.generation == 7 && snapshot->hd.recognition.candidate_id == 10 &&
           snapshot->hd.recognition.certainty_ppm == 930'000 &&
           snapshot->hd.recognition.confirmed && snapshot->hd.identity == MusicIdentityId{1} &&
           snapshot->hd.occurrence == OccurrenceId{2} &&
           snapshot->hd.appearance == AppearanceId{5} && snapshot->hd.visit == 6 &&
           snapshot->hd.iteration == 8 && snapshot->hd.bus_owner == AudioBusId{9} &&
           snapshot->hd.voices.size() == 1 && snapshot->hd.queue.has_value() &&
           snapshot->hd.envelopes.size() == 1 && snapshot->hd.pending.size() == 1 &&
           snapshot->hd.resource_revisions.size() == 1;
}

bool invalid_or_mixed_boundaries_are_rejected() {
    runtime::MusicSnapshotCoordinator coordinator;
    runtime::MusicHdSnapshotState hd;
    hd.generation = 3;
    hd.recognition = {7, 900'000, true};
    hd.identity = MusicIdentityId{1};
    hd.occurrence = OccurrenceId{2};
    hd.node = SequenceNodeId{3};
    hd.segment = SequenceSegmentId{4};
    hd.appearance = AppearanceId{5};
    hd.bus_owner = AudioBusId{6};
    const std::array<std::uint8_t, 1> game{42};

    const auto first = coordinator.capture(100, game, hd);
    hd.generation = 4;
    const auto duplicate = coordinator.capture(100, game, hd);
    const auto older = coordinator.capture(99, game, hd);
    const auto next = coordinator.capture(101, game, hd);
    return first && !duplicate && !older && next && coordinator.last_recoverable_boundary() == 101;
}

} // namespace

int main() {
    if (!complete_state_is_captured_at_one_boundary() ||
        !invalid_or_mixed_boundaries_are_rejected()) {
        std::fprintf(stderr, "FAIL: coherent music snapshot contract\n");
        return 1;
    }
    return 0;
}
