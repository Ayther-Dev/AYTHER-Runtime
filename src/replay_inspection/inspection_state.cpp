#include "inspection_state.h"

#include <utility>

namespace ayther::replay_inspection {
namespace {

Command notice(Notice value) { return {CommandKind::notice, 0, value, {}}; }

} // namespace

InspectionController::InspectionController(std::uint32_t frames, bool last_take) noexcept
    : frames_(frames), last_take_(last_take) {}

std::uint32_t InspectionController::next_frame() const noexcept {
    return position_ ? *position_ + 1U : 0U;
}

Commands InspectionController::event(std::string control, std::uint32_t frame) const {
    return {{CommandKind::inspection_event, frame, Notice::none, std::move(control)}};
}

Commands InspectionController::prepared() {
    if (phase_ == InspectionPhase::preparing)
        phase_ = InspectionPhase::playing;
    return {};
}

bool start_frame_now(InspectionPhase phase, std::chrono::steady_clock::time_point now,
                     std::chrono::steady_clock::time_point due) noexcept {
    if (phase == InspectionPhase::pausing)
        return true;
    return phase == InspectionPhase::playing && now >= due;
}

Commands InspectionController::pause_at(std::uint32_t frame) {
    phase_ = InspectionPhase::paused;
    position_ = frame;
    Commands commands{{CommandKind::present_frame, frame, Notice::none, {}},
                      {CommandKind::pause_audio, frame, Notice::none, {}}};
    auto paused = event("pause", frame);
    commands.insert(commands.end(), paused.begin(), paused.end());
    return commands;
}

Commands InspectionController::key(KeyAction action, FrameActivity activity) {
    if (phase_ == InspectionPhase::closing || phase_ == InspectionPhase::failed)
        return {};
    // RF-6.2, RF-6.3: I never changes the state or the position.
    if (action == KeyAction::info) {
        if (phase_ == InspectionPhase::preparing)
            return {};
        debug_visible_ = !debug_visible_;
        return {{CommandKind::toggle_debug, position_.value_or(0U), Notice::none, {}}};
    }
    switch (phase_) {
    case InspectionPhase::preparing:
        // RF-4.10, RF-5.11: nothing is left pending.
        return action == KeyAction::toggle ? Commands{notice(Notice::not_available)} : Commands{};
    case InspectionPhase::playing:
        // RF-5.9: arrows do nothing while playing.
        if (action != KeyAction::toggle)
            return {};
        // RF-4.1: without a frame in progress the last completed one stays, presented in pause;
        // the next frame is not started. A frame in progress (or frame 0) finishes first.
        if (activity == FrameActivity::between_frames && position_)
            return pause_at(*position_);
        phase_ = InspectionPhase::pausing;
        return {};
    case InspectionPhase::pausing:
        return action == KeyAction::toggle ? Commands{notice(Notice::not_available)} : Commands{};
    case InspectionPhase::recovering:
        return {notice(Notice::busy)};
    case InspectionPhase::interrupted:
        return action == KeyAction::toggle ? Commands{notice(Notice::interrupted)} : Commands{};
    case InspectionPhase::paused:
        break;
    case InspectionPhase::closing:
    case InspectionPhase::failed:
        return {};
    }

    const auto frame = position_.value_or(0U);
    if (action == KeyAction::toggle) {
        if (frame + 1U < frames_ && !ended_naturally_) {
            phase_ = InspectionPhase::playing;
            Commands commands{{CommandKind::resume_audio, frame + 1U, Notice::none, {}}};
            auto resumed = event("resume", frame);
            commands.insert(commands.end(), resumed.begin(), resumed.end());
            return commands;
        }
        // RF-4.8: at N−1 Space starts the next take, if there is one.
        if (!last_take_) {
            phase_ = InspectionPhase::closing;
            auto commands = event("advance_take", frame);
            commands.push_back({CommandKind::advance_take, frame, Notice::none, {}});
            return commands;
        }
        return {notice(Notice::no_next_frame)};
    }
    // RF-5.1, RF-5.5: one frame, bounded to the take; never another take.
    const bool back = action == KeyAction::left;
    if ((back && frame == 0U) || (!back && frame + 1U >= frames_))
        return {};
    target_ = back ? frame - 1U : frame + 1U;
    phase_ = InspectionPhase::recovering;
    inspected_ = true;
    Commands commands{{CommandKind::recover_frame, *target_, Notice::none, {}}};
    auto stepped = event(back ? "step_back" : "step_forward", *target_);
    commands.insert(commands.end(), stepped.begin(), stepped.end());
    return commands;
}

Commands InspectionController::frame_completed(std::uint32_t frame) {
    if (phase_ != InspectionPhase::playing && phase_ != InspectionPhase::pausing)
        return {};
    const bool pause_pending = phase_ == InspectionPhase::pausing;
    position_ = frame;
    if (frame + 1U < frames_) {
        // RF-4.1: the pause happens at the end of the frame in progress.
        return pause_pending ? pause_at(frame) : Commands{};
    }
    // §5.11: natural end at N−1.
    ended_naturally_ = true;
    if (pause_pending)
        return pause_at(frame);
    if (!last_take_) {
        // The next take starts on the next cadence slot (take_slot_reached).
        take_end_pending_ = true;
        return {};
    }
    // The last take confirms its linear result first, then stays paused at N−1.
    phase_ = InspectionPhase::paused;
    return {{CommandKind::confirm_linear_result, frame, Notice::none, {}},
            {CommandKind::present_frame, frame, Notice::none, {}},
            {CommandKind::pause_audio, frame, Notice::none, {}}};
}

Commands InspectionController::recovery_finished(RecoveryOutcome outcome) {
    if (phase_ != InspectionPhase::recovering || !target_)
        return {};
    const auto target = *target_;
    target_.reset();
    switch (outcome) {
    case RecoveryOutcome::presented:
        phase_ = InspectionPhase::paused;
        position_ = target;
        return {{CommandKind::present_frame, target, Notice::none, {}}};
    case RecoveryOutcome::failed_restored:
        phase_ = InspectionPhase::paused;
        return {notice(Notice::recovery_failed),
                {CommandKind::present_frame, position_.value_or(0U), Notice::none, {}}};
    case RecoveryOutcome::failed_unrecoverable:
        phase_ = InspectionPhase::failed;
        return {{CommandKind::finish_traversal, position_.value_or(0U), Notice::none, {}}};
    }
    return {};
}

Commands InspectionController::presentation_interrupted(std::string cause) {
    if (phase_ == InspectionPhase::closing || phase_ == InspectionPhase::failed)
        return {};
    phase_ = InspectionPhase::interrupted;
    target_.reset();
    cause_ = std::move(cause);
    return event("interrupted", position_.value_or(0U));
}

Commands InspectionController::presentation_recovered() {
    if (phase_ != InspectionPhase::interrupted)
        return {};
    phase_ = InspectionPhase::paused;
    auto commands = event("recovered", position_.value_or(0U));
    commands.push_back({CommandKind::present_frame, position_.value_or(0U), Notice::none, {}});
    return commands;
}

Commands InspectionController::take_slot_reached() {
    if (!take_end_pending())
        return {};
    take_end_pending_ = false;
    phase_ = InspectionPhase::closing;
    return {{CommandKind::advance_take, position_.value_or(0U), Notice::none, {}}};
}

Commands InspectionController::cancel() {
    // RF-2.10: the cancellation is attended in any state and discards what is pending.
    phase_ = InspectionPhase::closing;
    target_.reset();
    return {{CommandKind::close, position_.value_or(0U), Notice::none, {}}};
}

} // namespace ayther::replay_inspection
