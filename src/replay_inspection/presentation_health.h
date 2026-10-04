#pragma once

#include <cstdint>
#include <string>

namespace ayther::replay_inspection {

// Spec 002, plan §5.10 (RF-2.6, RF-4.9, RF-4.11): the health of the visible presentation.
// Any loss interrupts and the controller stops at the last completed frame; every poll
// tries to recover; DEVICE_LOST cannot recover with the current context; losing focus is
// not an interruption.
enum class PresentationState { healthy, interrupted, lost };
enum class VideoResult { presented, acquire_failed, present_failed, device_lost };
enum class RecoveryAttempt { healthy, recovered, still_interrupted, unrecoverable };

class PresentationHealth final {
  public:
    void frame_result(VideoResult result);
    // Only the device the replay uses counts.
    void audio_device_removed(bool in_use);
    void audio_device_added() noexcept;
    void minimized(bool value);
    void focus(bool focused) noexcept;

    // The adapter tried to rebuild the swapchain and to reopen the default audio device.
    RecoveryAttempt poll(bool swapchain_ready, bool audio_ready);

    [[nodiscard]] PresentationState state() const noexcept { return state_; }
    [[nodiscard]] const std::string &cause() const noexcept { return cause_; }
    // Interruptions recorded as incomplete audiovisual observations.
    [[nodiscard]] std::uint32_t incidents() const noexcept { return incidents_; }

  private:
    void interrupt(std::string cause);

    PresentationState state_{PresentationState::healthy};
    std::string cause_;
    std::uint32_t incidents_{};
    bool video_lost_{};
    bool audio_lost_{};
    bool minimized_{};
};

} // namespace ayther::replay_inspection
