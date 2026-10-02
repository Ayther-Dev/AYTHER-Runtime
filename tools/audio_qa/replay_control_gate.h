#pragma once

#include <cstdint>
#include <string_view>

namespace ayther::audio_qa {

enum class ReplayExternalControl {
    game_keyboard,
    rewind,
    fast_forward,
    cancel,
};

enum class ReplayControlDisposition {
    ignored,
    cancel_requested,
};

struct ReplayExternalControlCommand {
    ReplayExternalControl control{};
    std::uint16_t game_buttons{};
};

struct ReplayControlResult {
    ReplayControlDisposition disposition{ReplayControlDisposition::ignored};
    std::string_view code;
};

class ReplayControlGate final {
  public:
    [[nodiscard]] ReplayControlResult handle(ReplayExternalControlCommand command) noexcept;

    [[nodiscard]] bool cancellation_requested() const noexcept;

  private:
    bool cancellation_requested_{};
};

} // namespace ayther::audio_qa
