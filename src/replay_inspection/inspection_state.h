#pragma once

#include "key_router.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ayther::replay_inspection {

// Spec 002, plan §5.4 and §5.11 (RF-2.6, RF-2.8, RF-2.9, RF-2.10, RF-3, RF-4, RF-5, RF-6.2):
// the inspection state machine. It receives routed keys, frame completions, recovery
// results, presentation health and the cancellation, and returns orders for the adapter.
enum class InspectionPhase {
    preparing,
    playing,
    pausing,
    paused,
    recovering,
    interrupted,
    closing,
    failed,
};

enum class CommandKind {
    present_frame,
    recover_frame,
    pause_audio,
    resume_audio,
    notice,
    toggle_debug,
    inspection_event,
    confirm_linear_result,
    advance_take,
    finish_traversal,
    close,
};

enum class Notice { none, not_available, busy, recovery_failed, interrupted, no_next_frame };

struct Command {
    CommandKind kind{CommandKind::notice};
    std::uint32_t frame{};
    Notice notice{Notice::none};
    // The control of an inspection event (contracts.md C2).
    std::string control;
};

enum class RecoveryOutcome { presented, failed_restored, failed_unrecoverable };

using Commands = std::vector<Command>;

class InspectionController final {
  public:
    InspectionController(std::uint32_t frames, bool last_take) noexcept;

    Commands prepared();
    Commands key(KeyAction action);
    // The adapter produced frame k (playing or finishing a pause).
    Commands frame_completed(std::uint32_t frame);
    Commands recovery_finished(RecoveryOutcome outcome);
    Commands presentation_interrupted(std::string cause);
    Commands presentation_recovered();
    Commands cancel();

    [[nodiscard]] InspectionPhase phase() const noexcept { return phase_; }
    // Only `playing` starts frames.
    [[nodiscard]] bool may_run_frame() const noexcept { return phase_ == InspectionPhase::playing; }
    [[nodiscard]] std::optional<std::uint32_t> position() const noexcept { return position_; }
    [[nodiscard]] std::uint32_t next_frame() const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> target() const noexcept { return target_; }
    [[nodiscard]] bool debug_visible() const noexcept { return debug_visible_; }
    [[nodiscard]] bool traversal_inspected() const noexcept { return inspected_; }
    [[nodiscard]] bool ended_naturally() const noexcept { return ended_naturally_; }
    [[nodiscard]] const std::string &interruption_cause() const noexcept { return cause_; }

  private:
    Commands pause_at(std::uint32_t frame);
    Commands event(std::string control, std::uint32_t frame) const;

    std::uint32_t frames_;
    bool last_take_;
    InspectionPhase phase_{InspectionPhase::preparing};
    std::optional<std::uint32_t> position_;
    std::optional<std::uint32_t> target_;
    bool debug_visible_{};
    bool inspected_{};
    bool ended_naturally_{};
    std::string cause_;
};

// P-2 (RF-4.1): whether the loop starts the next frame now. Playback waits for the turn of each
// frame (`due`); a requested pause finishes the frame in progress at once, without waiting for
// its turn, so that the paused image follows the key by one production and one presentation.
[[nodiscard]] bool start_frame_now(InspectionPhase phase, std::chrono::steady_clock::time_point now,
                                   std::chrono::steady_clock::time_point due) noexcept;

} // namespace ayther::replay_inspection
