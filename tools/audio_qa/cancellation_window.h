#pragma once

#include "model.h"

#include <cstdint>
#include <optional>

namespace ayther::audio_qa {

inline constexpr std::uint64_t cancellation_response_deadline_ms = 2000;
inline constexpr std::uint64_t cancellation_wait_deadline_ms = 5000;

enum class CancellationTrigger { channel_lost, no_progress, controlled_interrupt };
enum class CancellationDeadlineReason { response_timeout, wait_timeout };

struct CancellationDeadlineDiagnostic {
    CancellationTrigger trigger{CancellationTrigger::channel_lost};
    CancellationDeadlineReason reason{CancellationDeadlineReason::response_timeout};
    EvidenceResult evidence_result{EvidenceResult::incomplete};
    bool cancellation_attempted{};
    bool request_delivered{};
    bool response_received{};
    bool cessation_confirmed{};
    std::uint64_t detected_ms{};
    std::uint64_t requested_ms{};
    std::uint64_t elapsed_since_request_ms{};
};

class CancellationWindow final {
  public:
    CancellationWindow(CancellationTrigger trigger, std::uint64_t detected_ms) noexcept;

    [[nodiscard]] bool attempt_cancel(std::uint64_t now_ms, bool request_delivered) noexcept;
    [[nodiscard]] bool observe_response(std::uint64_t now_ms) noexcept;
    [[nodiscard]] bool confirm_cessation(std::uint64_t now_ms) noexcept;

    [[nodiscard]] std::optional<CancellationDeadlineDiagnostic>
    check(std::uint64_t now_ms) const noexcept;

  private:
    CancellationTrigger trigger_;
    std::uint64_t detected_ms_{};
    std::optional<std::uint64_t> requested_ms_;
    std::optional<std::uint64_t> response_ms_;
    std::optional<std::uint64_t> cessation_ms_;
    bool request_delivered_{};
};

} // namespace ayther::audio_qa
