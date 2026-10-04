// Spec 002, BR-129 (RF-2.6, RF-4.9, RF-4.11; plan §5.10): the health of the visible
// presentation. A lost video frame, the removal of the audio device in use or minimizing
// interrupts; each poll tries to recover; DEVICE_LOST does not recover; losing focus is
// not an interruption.
#include "presentation_health.h"

#include <iostream>
#include <string_view>

namespace ri = ayther::replay_inspection;
using State = ri::PresentationState;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

} // namespace

int main() {
    ri::PresentationHealth video;
    expect(video.state() == State::healthy, "the presentation starts healthy");
    video.frame_result(ri::VideoResult::acquire_failed);
    expect(video.state() == State::interrupted && video.cause() == "video_acquire_failed",
           "RF-2.6: losing a video frame interrupts with its cause");
    expect(video.poll(true, true) == ri::RecoveryAttempt::recovered &&
               video.state() == State::healthy && video.incidents() == 1U,
           "RF-2.6: a successful poll recovers and keeps the incident");

    ri::PresentationHealth audio;
    audio.audio_device_removed(true);
    expect(audio.state() == State::interrupted && audio.cause() == "audio_device_removed",
           "RF-2.6: removing the audio device in use interrupts");
    expect(audio.poll(true, false) == ri::RecoveryAttempt::still_interrupted,
           "RF-2.6: while the device is not reopened it stays interrupted");
    audio.audio_device_added();
    expect(audio.poll(true, true) == ri::RecoveryAttempt::recovered, "RF-2.6: reopening recovers");
    ri::PresentationHealth other;
    other.audio_device_removed(false);
    expect(other.state() == State::healthy,
           "RF-2.6: removing a device not in use is not an interruption");

    ri::PresentationHealth window;
    window.minimized(true);
    expect(window.state() == State::interrupted && window.cause() == "window_minimized",
           "RF-4.11: minimizing interrupts");
    expect(window.poll(true, true) == ri::RecoveryAttempt::still_interrupted,
           "RF-4.11: a minimized window cannot present");
    window.minimized(false);
    expect(window.poll(true, true) == ri::RecoveryAttempt::recovered,
           "RF-4.11: restoring the window recovers");

    ri::PresentationHealth lost;
    lost.frame_result(ri::VideoResult::device_lost);
    expect(lost.state() == State::lost &&
               lost.poll(true, true) == ri::RecoveryAttempt::unrecoverable &&
               lost.state() == State::lost,
           "plan §5.10: DEVICE_LOST stays interrupted until the user cancels");

    ri::PresentationHealth focus;
    focus.focus(false);
    focus.frame_result(ri::VideoResult::presented);
    expect(focus.state() == State::healthy && focus.incidents() == 0U,
           "RF-4.9: losing focus is not an interruption");
    if (failures != 0)
        return 1;
    std::cout << "presentation interruptions follow plan §5.10\n";
    return 0;
}
