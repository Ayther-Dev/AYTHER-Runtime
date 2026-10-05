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

// Where the take is when a key arrives (spec.md, Pausa; RF-4.1): producing a frame, or waiting
// between two. A pause while producing finishes that frame; between frames there is no frame in
// progress and the last completed one stays.
enum class FrameActivity { between_frames, producing };

class InspectionController final {
  public:
    InspectionController(std::uint32_t frames, bool last_take) noexcept;

    Commands prepared();
    Commands key(KeyAction action, FrameActivity activity = FrameActivity::between_frames);
    // The adapter produced frame k (playing or finishing a pause).
    Commands frame_completed(std::uint32_t frame);
    // RF-2.8, RF-4.1 (DI-13): after N−1 of an intermediate take the next take starts on the next
    // cadence slot, not at once, so that a pause asked for in that wait stays on N−1. The adapter
    // calls this when the slot comes; it gives way to the next take unless a pause came first.
    Commands take_slot_reached();
    Commands recovery_finished(RecoveryOutcome outcome);
    Commands presentation_interrupted(std::string cause);
    Commands presentation_recovered();
    Commands cancel();

    [[nodiscard]] InspectionPhase phase() const noexcept { return phase_; }
    // Only `playing` starts frames, and not while waiting for the slot of the next take.
    [[nodiscard]] bool may_run_frame() const noexcept {
        return phase_ == InspectionPhase::playing && !take_end_pending_;
    }
    [[nodiscard]] bool take_end_pending() const noexcept {
        return phase_ == InspectionPhase::playing && take_end_pending_;
    }
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
    bool take_end_pending_{};
    std::string cause_;
};

// P-2 (RF-4.1): whether the loop starts the next frame now. Playback waits for the turn of each
// frame (`due`); a pause pending on a frame in progress (or on frame 0) finishes it at once.
[[nodiscard]] bool start_frame_now(InspectionPhase phase, std::chrono::steady_clock::time_point now,
                                   std::chrono::steady_clock::time_point due) noexcept;

// Plan §5.7 (RF-4.4, RF-4.6, RNF-2): the turn of each frame of continuous playback. The turn
// starts at the first present after the preparation (D-6a), as it restarts at a resume. From
// then on every completed frame moves the turn one period; a frame that completes more than one
// period after its turn is late (`cadence_degraded`, spec-001 semantics), and the caller may
// restart the turn there instead of catching up.
class PlaybackCadence final {
  public:
    using Clock = std::chrono::steady_clock;

    PlaybackCadence(Clock::duration period, Clock::time_point now) noexcept;

    // When the next frame may start.
    [[nodiscard]] Clock::time_point due() const noexcept { return next_; }
    // RF-4.4: resuming restarts the turn at this instant; the pause is not caught up.
    void restart(Clock::time_point now) noexcept;
    // A frame of continuous playback completed at `completed`; true when it was late.
    [[nodiscard]] bool frame_completed(Clock::time_point completed) noexcept;

  private:
    Clock::duration period_;
    Clock::time_point next_;
    bool anchored_{};
};

} // namespace ayther::replay_inspection
