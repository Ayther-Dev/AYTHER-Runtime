#pragma once

#include "replay_control_gate.h"
#include "run_lifecycle.h"

#include <cstdint>
#include <optional>

namespace ayther::audio_qa {

struct ReplayCancellationRecord {
    bool requested{};
    bool applied{};
    std::uint32_t requested_after_frames{};
    std::uint32_t applied_after_frames{};
    std::optional<std::uint32_t> last_executed_recording_frame;
    std::optional<std::uint64_t> last_engine_frame;
};

class ReplayCancellation final {
  public:
    [[nodiscard]] bool capture_request(const ReplayControlGate &controls,
                                       std::uint32_t frames_completed,
                                       std::optional<std::uint64_t> last_engine_frame) noexcept;

    [[nodiscard]] bool apply_at_frame_boundary(std::uint32_t frames_completed,
                                               std::optional<std::uint64_t> last_engine_frame,
                                               RunLifecycle &lifecycle) noexcept;

    [[nodiscard]] const ReplayCancellationRecord &record() const noexcept;

  private:
    ReplayCancellationRecord record_;
};

} // namespace ayther::audio_qa
