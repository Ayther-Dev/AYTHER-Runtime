#pragma once

#include "run_lifecycle.h"

#include <cstdint>
#include <optional>

namespace ayther::audio_qa {

struct ReplayTerminalEvent {
    PlaybackResult result{PlaybackResult::not_started};
    std::uint32_t frames_completed{};
    std::optional<std::uint32_t> last_executed_recording_frame;
    std::optional<std::uint64_t> last_engine_frame;
    bool operator==(const ReplayTerminalEvent &) const = default;
};

enum class ReplayTerminalDecision { accepted, already_resolved, invalid };

class ReplayTerminalArbiter final {
  public:
    [[nodiscard]] ReplayTerminalDecision resolve(ReplayTerminalEvent event,
                                                 RunLifecycle &lifecycle) noexcept;

    [[nodiscard]] const std::optional<ReplayTerminalEvent> &terminal() const noexcept;

  private:
    std::optional<ReplayTerminalEvent> terminal_;
};

} // namespace ayther::audio_qa
