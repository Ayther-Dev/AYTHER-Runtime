// Spec 002, plan §5.3 (RF-2.5, RF-2.10): one test per case of the request terminal
// arbiter. A cancellation before confirming wins and discards pending controls; a
// confirmed result never changes; a failure stops the request with its diagnostic.
#include "request_terminal_arbiter.h"

#include <iostream>
#include <optional>
#include <string_view>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

const qa::PlaybackOutcome natural{qa::PlaybackKind::natural_end, {}};
const qa::PlaybackOutcome cancelled{qa::PlaybackKind::cancelled, {}};

void cancellation_before_confirmation() {
    qa::RequestTerminalArbiter arbiter;
    arbiter.begin_take(0);
    expect(arbiter.queue_control() && arbiter.queue_control() && arbiter.pending_controls() == 2U,
           "RF-2.10: inspection controls queue while the take runs");
    expect(arbiter.cancel() && arbiter.playback() == cancelled,
           "RF-2.5: a cancellation before confirming ends the take as cancelled");
    expect(arbiter.pending_controls() == 0U && arbiter.discarded_controls() == 2U,
           "RF-2.10: the cancellation discards the pending controls");
    expect(!arbiter.queue_control() && arbiter.discarded_controls() == 3U,
           "RF-2.10: no control is accepted after the cancellation");
    expect(arbiter.confirm() == cancelled && arbiter.confirmed(),
           "RF-2.5: the cancelled result is confirmed");
    expect(arbiter.stop_requested() &&
               arbiter.not_started_reason() == "not_started_after_cancellation",
           "RF-2.5: the pending takes are left without starting");
}

void cancellation_after_confirmation() {
    qa::RequestTerminalArbiter arbiter;
    arbiter.begin_take(0);
    expect(arbiter.natural_end() && arbiter.confirm() == natural,
           "RF-2.10: the natural end is confirmed");
    expect(!arbiter.cancel() && arbiter.playback() == natural && arbiter.confirm() == natural,
           "RF-2.10: a confirmed result does not change with a later cancellation");
    expect(arbiter.stop_requested() &&
               arbiter.not_started_reason() == "not_started_after_cancellation",
           "RF-2.5: the later cancellation still stops the takes not started");
    arbiter.begin_take(1);
    expect(arbiter.stop_requested(), "RF-2.5: the stop survives the next take");
}

void cancellation_and_natural_end_together() {
    qa::RequestTerminalArbiter natural_first;
    natural_first.begin_take(0);
    expect(natural_first.natural_end() && natural_first.cancel() &&
               natural_first.confirm() == cancelled,
           "RF-2.10: a cancellation after an unconfirmed natural end is recorded as such");
    qa::RequestTerminalArbiter cancel_first;
    cancel_first.begin_take(0);
    expect(cancel_first.cancel() && !cancel_first.natural_end() &&
               cancel_first.confirm() == cancelled,
           "RF-2.10: a natural end after a cancellation does not change it");
}

void failure_stops_the_request() {
    qa::RequestTerminalArbiter arbiter;
    arbiter.begin_take(1);
    expect(arbiter.queue_control(), "a control is accepted before the failure");
    expect(arbiter.fail("runtime_exited") &&
               arbiter.playback() ==
                   qa::PlaybackOutcome{qa::PlaybackKind::failed, "runtime_exited"},
           "RF-2.5: a failure ends the take with its diagnostic");
    expect(arbiter.discarded_controls() == 1U && !arbiter.queue_control(),
           "RF-2.10: a failure also discards the pending controls");
    expect(!arbiter.cancel() && !arbiter.natural_end() &&
               arbiter.confirm() == qa::PlaybackOutcome{qa::PlaybackKind::failed, "runtime_exited"},
           "RF-2.10: the first terminal event decides the take");
    expect(arbiter.not_started_reason() == "not_started_after_failure",
           "RF-2.5: a failure closes the request like a cancellation");

    qa::RequestTerminalArbiter interrupted;
    interrupted.begin_take(0);
    expect(interrupted.fail("presentation_lost", true) &&
               interrupted.confirm() ==
                   qa::PlaybackOutcome{qa::PlaybackKind::interrupted, "presentation_lost"},
           "RF-2.6: an interruption keeps its own kind");
}

void nothing_decided() {
    qa::RequestTerminalArbiter arbiter;
    expect(!arbiter.cancel() && arbiter.stop_requested(),
           "RF-2.5: a cancellation between takes only stops the request");
    arbiter.begin_take(0);
    expect(!arbiter.playback() && !arbiter.confirm() && !arbiter.confirmed(),
           "nothing is confirmed while the take has not ended");
}

} // namespace

int main() {
    cancellation_before_confirmation();
    cancellation_after_confirmation();
    cancellation_and_natural_end_together();
    failure_stops_the_request();
    nothing_decided();
    if (failures != 0)
        return 1;
    std::cout << "the request terminal arbiter decides coinciding events\n";
    return 0;
}
