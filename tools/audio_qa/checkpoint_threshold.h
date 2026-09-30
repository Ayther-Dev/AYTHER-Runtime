#pragma once

#include "checkpoint_store.h"

#include <cstdint>
#include <optional>

namespace ayther::audio_qa {

inline constexpr std::uint64_t checkpoint_interval_ms = 1'000;
inline constexpr std::uint64_t checkpoint_pending_byte_limit = 1U << 20U;

enum class CheckpointPublicationAction { none, publish, saturated };

struct CheckpointPublicationRequest {
    CheckpointPublicationAction action{CheckpointPublicationAction::none};
    std::uint64_t publication_bytes{};
    std::uint64_t pending_bytes{};
    bool time_threshold{};
    bool byte_threshold{};
    bool operator==(const CheckpointPublicationRequest &) const = default;
};

class CheckpointThreshold final {
  public:
    explicit CheckpointThreshold(std::uint64_t started_at_ms) noexcept;

    [[nodiscard]] bool record_received(std::uint64_t total_bytes, std::uint64_t now_ms) noexcept;
    [[nodiscard]] CheckpointPublicationRequest request_publication(std::uint64_t now_ms) noexcept;
    [[nodiscard]] bool confirm_publication(const StoredCheckpoint &checkpoint,
                                           std::uint64_t now_ms) noexcept;

    [[nodiscard]] std::uint64_t received_bytes() const noexcept;
    [[nodiscard]] std::uint64_t durable_bytes() const noexcept;
    [[nodiscard]] std::uint64_t last_checkpoint_sequence() const noexcept;
    [[nodiscard]] bool publication_pending() const noexcept;

  private:
    std::uint64_t received_bytes_{};
    std::uint64_t durable_bytes_{};
    std::uint64_t last_checkpoint_sequence_{};
    std::uint64_t last_observed_at_ms_{};
    std::optional<std::uint64_t> pending_since_ms_;
    std::optional<std::uint64_t> publication_bytes_;
    std::optional<std::uint64_t> post_request_pending_since_ms_;
};

} // namespace ayther::audio_qa
