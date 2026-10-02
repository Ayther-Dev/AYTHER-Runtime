#pragma once

#include "model.h"

#include <cstdint>
#include <optional>

namespace ayther::audio_qa {

enum class ProcessCessationState {
    connected,
    disconnected,
    stop_requested,
    unconfirmed,
    confirmed,
};

struct ProcessCessationStatus {
    ProcessCessationState state{ProcessCessationState::connected};
    Run last_known_run;
    std::optional<std::int32_t> exit_code;
};

class ProcessCessation final {
  public:
    explicit ProcessCessation(Run last_known_run) noexcept;

    [[nodiscard]] bool observe_run(const Run &run) noexcept;
    [[nodiscard]] bool observe_disconnect() noexcept;
    [[nodiscard]] bool request_stop() noexcept;
    [[nodiscard]] bool mark_unconfirmed() noexcept;
    [[nodiscard]] bool confirm_exit(std::int32_t exit_code) noexcept;

    [[nodiscard]] const ProcessCessationStatus &status() const noexcept;

  private:
    ProcessCessationStatus status_;
};

} // namespace ayther::audio_qa
