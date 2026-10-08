#pragma once

#include "model.h"

#include <cstdint>
#include <optional>
#include <string>

namespace ayther::audio_qa {

enum class WatchdogPhase { preparation, playback, closing };
enum class WatchdogTimeoutReason { no_progress, total_time };

struct WatchdogPolicy {
    std::uint64_t no_progress_ms{};
    std::uint64_t total_ms{};
};

inline constexpr WatchdogPolicy preparation_watchdog_policy{120000, 300000};
inline constexpr WatchdogPolicy playback_watchdog_policy{5000, 0};
inline constexpr WatchdogPolicy closing_watchdog_policy{10000, 0};

struct WatchdogTimeout {
    WatchdogPhase phase{WatchdogPhase::preparation};
    WatchdogTimeoutReason reason{WatchdogTimeoutReason::no_progress};
    EvidenceResult evidence_result{EvidenceResult::incomplete};
    std::string last_preparation_stage;
    std::optional<std::uint32_t> last_completed_frame;
    std::uint64_t bytes_received{};
    std::uint64_t bytes_durable{};
    std::uint64_t last_progress_ms{};
    std::uint64_t elapsed_total_ms{};
};

// Campaign 2026-10-07: the progress of a position that may move back, such as the end of the
// PCM captured by an inspection while it closes. Every change of the position adds its
// distance, so the progress only grows and stops growing when the position stops moving.
class MonotonicProgress final {
  public:
    [[nodiscard]] std::uint64_t observe(std::uint64_t position) noexcept;

  private:
    std::optional<std::uint64_t> last_;
    std::uint64_t total_{};
};

class ProgressWatchdog final {
  public:
    ProgressWatchdog(WatchdogPhase phase, std::uint64_t started_ms) noexcept;

    [[nodiscard]] bool observe_heartbeat(std::uint64_t now_ms) noexcept;
    [[nodiscard]] bool observe_preparation_stage(std::string stage, std::uint64_t now_ms);
    [[nodiscard]] bool observe_completed_frame(std::uint32_t frame, std::uint64_t now_ms) noexcept;
    [[nodiscard]] bool observe_bytes_received(std::uint64_t bytes, std::uint64_t now_ms) noexcept;
    [[nodiscard]] bool observe_bytes_durable(std::uint64_t bytes, std::uint64_t now_ms) noexcept;

    [[nodiscard]] std::optional<WatchdogTimeout> check(std::uint64_t now_ms) const noexcept;

  private:
    [[nodiscard]] bool valid_time(std::uint64_t now_ms) const noexcept;
    [[nodiscard]] WatchdogPolicy policy() const noexcept;

    WatchdogPhase phase_;
    std::uint64_t started_ms_{};
    std::uint64_t last_observed_ms_{};
    std::uint64_t last_progress_ms_{};
    std::string last_preparation_stage_;
    std::optional<std::uint32_t> last_completed_frame_;
    std::uint64_t bytes_received_{};
    std::uint64_t bytes_durable_{};
};

} // namespace ayther::audio_qa
