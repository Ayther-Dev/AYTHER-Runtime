#pragma once

#include "capabilities.h"

#include <string_view>

namespace ayther::audio_qa {

enum class CapabilityGateState { pending, accepted, rejected, game_started };

using StartQaGameOperation = bool (*)(void *context) noexcept;

class CapabilityGate final {
  public:
    [[nodiscard]] bool negotiate(const CapabilitySet &offer);
    [[nodiscard]] bool start_game(void *context, StartQaGameOperation operation) noexcept;

    [[nodiscard]] CapabilityGateState state() const noexcept;
    [[nodiscard]] const CapabilityCheck &check() const noexcept;
    [[nodiscard]] std::string_view diagnostic_code() const noexcept;

  private:
    CapabilityGateState state_{CapabilityGateState::pending};
    CapabilityCheck check_;
};

} // namespace ayther::audio_qa
