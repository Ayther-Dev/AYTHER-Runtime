#pragma once

#include <ayther/engine/music_sequence.hpp>

#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

namespace runtime {

struct MusicVoiceSnapshot {
    std::uint64_t voice_id{};
    std::uint64_t source_cursor{};
    std::uint64_t output_cursor{};
    std::uint64_t generation{};
};

struct MusicQueueSnapshot {
    std::uint64_t event_id{};
    ayther::engine::SequenceNodeId destination{};
    std::uint64_t generation{};
    ayther::engine::AppearanceId appearance{};
};

struct MusicEnvelopeSnapshot {
    std::uint64_t envelope_id{};
    std::uint64_t cursor{};
    std::uint64_t length{};
};

struct MusicPendingSnapshot {
    std::uint64_t operation_id{};
    std::uint64_t generation{};
    std::uint64_t deadline_music_time{};
};

struct MusicResourceRevision {
    ayther::engine::AssetId asset{};
    std::uint64_t revision{};
};

struct MusicRecognitionSnapshot {
    std::uint64_t candidate_id{};
    std::uint32_t certainty_ppm{};
    bool confirmed{};
};

struct MusicHdSnapshotState {
    std::uint64_t generation{};
    MusicRecognitionSnapshot recognition;
    ayther::engine::MusicIdentityId identity{};
    ayther::engine::OccurrenceId occurrence{};
    ayther::engine::SequenceNodeId node{};
    ayther::engine::SequenceSegmentId segment{};
    ayther::engine::AppearanceId appearance{};
    std::uint64_t visit{};
    std::uint64_t iteration{};
    ayther::engine::AudioBusId bus_owner{};
    std::vector<MusicVoiceSnapshot> voices;
    std::optional<MusicQueueSnapshot> queue;
    std::vector<MusicEnvelopeSnapshot> envelopes;
    std::vector<MusicPendingSnapshot> pending;
    std::vector<MusicResourceRevision> resource_revisions;
};

struct CoherentMusicSnapshot {
    std::uint64_t boundary{};
    std::vector<std::uint8_t> game_state;
    MusicHdSnapshotState hd;
};

class MusicSnapshotCoordinator {
  public:
    [[nodiscard]] std::optional<CoherentMusicSnapshot>
    capture(std::uint64_t boundary, std::span<const std::uint8_t> game_state,
            const MusicHdSnapshotState &hd) {
        if (boundary == 0 || boundary <= last_recoverable_boundary_ || game_state.empty() ||
            !valid(hd))
            return std::nullopt;

        CoherentMusicSnapshot snapshot;
        snapshot.boundary = boundary;
        snapshot.game_state.assign(game_state.begin(), game_state.end());
        snapshot.hd = hd;
        last_recoverable_boundary_ = boundary;
        return snapshot;
    }

    [[nodiscard]] std::uint64_t last_recoverable_boundary() const noexcept {
        return last_recoverable_boundary_;
    }

  private:
    [[nodiscard]] static bool valid(const MusicHdSnapshotState &hd) noexcept {
        const auto current_generation = [&](const auto &value) {
            return value.generation == hd.generation;
        };
        return hd.generation != 0 && hd.recognition.certainty_ppm <= 1'000'000 &&
               (!hd.recognition.confirmed || hd.recognition.candidate_id != 0) && hd.identity &&
               hd.occurrence && hd.node && hd.segment && hd.appearance && hd.bus_owner &&
               std::ranges::all_of(hd.voices, current_generation) &&
               std::ranges::all_of(hd.pending, current_generation) &&
               (!hd.queue || current_generation(*hd.queue)) &&
               std::ranges::all_of(hd.resource_revisions, [](const auto &value) {
                   return static_cast<bool>(value.asset) && value.revision != 0;
               });
    }

    std::uint64_t last_recoverable_boundary_{};
};

} // namespace runtime
