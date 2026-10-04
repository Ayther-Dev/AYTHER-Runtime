#pragma once

#include "request_outcome.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ayther::audio_qa {

// Spec 002, plan §5.3 (RF-2.5, RF-2.10): decides how the active take and the request end
// when events coincide. Pure; the supervisor feeds it the events in the order it observes
// them.
//
// 1. A cancellation before the take result is confirmed wins over the natural end and
//    discards the inspection controls still pending.
// 2. A confirmed result never changes; a later cancellation only stops the request.
// 3. A failure closes the request like a cancellation, with its own diagnostic. Between
//    a cancellation and a failure the first one observed decides the take.
//
// Once the request is stopped, the takes that did not start are marked with
// `not_started_reason()`.
class RequestTerminalArbiter final {
  public:
    // A new take becomes active; the request may already be stopped.
    void begin_take(std::size_t position) noexcept;

    // Each event returns whether it decided or changed the result of the active take.
    bool cancel() noexcept;
    bool fail(std::string diagnostic, bool interrupted = false);
    bool natural_end() noexcept;

    // An inspection control for the active take: accepted only while nothing ends it.
    bool queue_control() noexcept;
    void control_sent() noexcept;
    [[nodiscard]] std::size_t pending_controls() const noexcept { return pending_controls_; }
    [[nodiscard]] std::size_t discarded_controls() const noexcept { return discarded_controls_; }

    // The result of the active take as decided so far.
    [[nodiscard]] std::optional<PlaybackOutcome> playback() const;
    // Freezes the result of the active take; nullopt when nothing decided it.
    std::optional<PlaybackOutcome> confirm();
    [[nodiscard]] bool confirmed() const noexcept { return confirmed_; }

    [[nodiscard]] bool stop_requested() const noexcept { return stop_ != Stop::none; }
    [[nodiscard]] std::string_view not_started_reason() const noexcept;

  private:
    enum class Stop { none, cancellation, failure };

    void stop(Stop reason) noexcept;

    bool take_active_{};
    bool confirmed_{};
    bool cancelled_{};
    bool ended_naturally_{};
    std::optional<PlaybackOutcome> failure_;
    std::optional<PlaybackOutcome> confirmed_result_;
    std::size_t pending_controls_{};
    std::size_t discarded_controls_{};
    Stop stop_{Stop::none};
};

} // namespace ayther::audio_qa
