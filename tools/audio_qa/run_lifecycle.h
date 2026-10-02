#pragma once

#include "model.h"

namespace ayther::audio_qa {

enum class RunTransitionError {
    none,
    invalid_initial_state,
    invalid_transition,
    invalid_terminal_result,
    invalid_evidence_result,
};

class RunLifecycle final {
  public:
    explicit RunLifecycle(Run run) noexcept;

    [[nodiscard]] RunTransitionError mark_ready() noexcept;
    [[nodiscard]] RunTransitionError start_playback() noexcept;
    [[nodiscard]] RunTransitionError begin_closing(PlaybackResult playback_result) noexcept;
    [[nodiscard]] RunTransitionError finish_closing(EvidenceResult evidence_result) noexcept;

    [[nodiscard]] const Run &run() const noexcept;
    [[nodiscard]] RunTransitionError error() const noexcept;

  private:
    Run run_;
    RunTransitionError error_{RunTransitionError::none};
};

} // namespace ayther::audio_qa
