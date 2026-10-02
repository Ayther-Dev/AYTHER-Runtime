#pragma once

#include <ayther/engine/music_sequence.hpp>

namespace runtime {

/// Runtime adapter over Engine's public RF-18 sequence model. Runtime owns no
/// duplicate graph or counter layout; it only drives the shared traversal.
class MusicSequenceRuntime {
  public:
    explicit MusicSequenceRuntime(
        const ayther::engine::MusicSequenceDefinition &definition) noexcept
        : traversal_(definition) {}

    [[nodiscard]] bool start(const ayther::engine::OccurrenceId occurrence) {
        return traversal_.enter(occurrence);
    }

    [[nodiscard]] bool transition_to(const ayther::engine::SequenceNodeId destination) {
        return traversal_.transition_to(destination);
    }

    [[nodiscard]] bool return_internal_loop() { return traversal_.return_internal_loop(); }

    [[nodiscard]] const ayther::engine::SequencePosition &position() const noexcept {
        return traversal_.position();
    }

  private:
    ayther::engine::SequenceTraversal traversal_;
};

} // namespace runtime
