#pragma once

#include "cancellation_message.h"
#include "cancellation_window.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace ayther::audio_qa {

struct CancellationSender {
    void *context{};
    bool (*send)(void *, std::span<const std::byte>) noexcept {};
};

enum class CheckCancellationState {
    idle,
    requested,
    acknowledged,
    applied,
    cessation_confirmed,
    response_missing,
    cessation_unknown,
};

struct CheckCancellationStatus {
    CheckCancellationState state{CheckCancellationState::idle};
    bool request_delivered{};
    bool response_received{};
    bool cessation_confirmed{};
};

class CheckCancellationCoordinator final {
  public:
    CheckCancellationCoordinator(std::string request_id, std::string run_id,
                                 std::uint64_t detected_ms);

    [[nodiscard]] bool request(std::uint64_t now_ms, CancellationSender sender) noexcept;
    [[nodiscard]] bool observe(const CancellationMessage &message, std::uint64_t now_ms) noexcept;
    [[nodiscard]] CheckCancellationStatus poll(std::uint64_t now_ms) noexcept;
    [[nodiscard]] const CheckCancellationStatus &status() const noexcept;

  private:
    std::string request_id_;
    std::string run_id_;
    CancellationWindow window_;
    CheckCancellationStatus status_;
};

} // namespace ayther::audio_qa
