#include "request_terminal_arbiter.h"

#include <utility>

namespace ayther::audio_qa {

void RequestTerminalArbiter::begin_take(std::size_t) noexcept {
    take_active_ = true;
    confirmed_ = false;
    cancelled_ = false;
    ended_naturally_ = false;
    failure_.reset();
    confirmed_result_.reset();
    pending_controls_ = 0;
    discarded_controls_ = 0;
}

bool RequestTerminalArbiter::cancel() noexcept {
    const bool decides = take_active_ && !confirmed_ && !cancelled_ && !failure_;
    if (decides) {
        cancelled_ = true;
        discarded_controls_ += pending_controls_;
        pending_controls_ = 0;
    }
    // Even after a confirmed result the cancellation stops the request: the takes that
    // have not started never start.
    stop(Stop::cancellation);
    return decides;
}

bool RequestTerminalArbiter::fail(std::string diagnostic, bool interrupted) {
    if (!take_active_ || confirmed_ || cancelled_ || failure_)
        return false;
    failure_ = PlaybackOutcome{interrupted ? PlaybackKind::interrupted : PlaybackKind::failed,
                               std::move(diagnostic)};
    discarded_controls_ += pending_controls_;
    pending_controls_ = 0;
    stop(Stop::failure);
    return true;
}

bool RequestTerminalArbiter::natural_end() noexcept {
    if (!take_active_ || confirmed_ || cancelled_ || failure_ || ended_naturally_)
        return false;
    ended_naturally_ = true;
    return true;
}

bool RequestTerminalArbiter::queue_control() noexcept {
    if (!take_active_ || cancelled_ || failure_) {
        ++discarded_controls_;
        return false;
    }
    ++pending_controls_;
    return true;
}

void RequestTerminalArbiter::control_sent() noexcept {
    if (pending_controls_ > 0U)
        --pending_controls_;
}

std::optional<PlaybackOutcome> RequestTerminalArbiter::playback() const {
    if (confirmed_)
        return confirmed_result_;
    if (cancelled_)
        return PlaybackOutcome{PlaybackKind::cancelled, {}};
    if (failure_)
        return failure_;
    if (ended_naturally_)
        return PlaybackOutcome{PlaybackKind::natural_end, {}};
    return std::nullopt;
}

std::optional<PlaybackOutcome> RequestTerminalArbiter::confirm() {
    if (confirmed_)
        return confirmed_result_;
    auto result = playback();
    if (result) {
        confirmed_ = true;
        confirmed_result_ = result;
    }
    return result;
}

std::string_view RequestTerminalArbiter::not_started_reason() const noexcept {
    switch (stop_) {
    case Stop::none:
        return {};
    case Stop::cancellation:
        return "not_started_after_cancellation";
    case Stop::failure:
        return "not_started_after_failure";
    }
    return {};
}

void RequestTerminalArbiter::stop(Stop reason) noexcept {
    if (stop_ == Stop::none)
        stop_ = reason;
}

} // namespace ayther::audio_qa
