#pragma once

#include "cancel_token.h"

#include <csignal>

namespace ayther::audio_qa {

class ControlledInterrupt final {
  public:
    void request() noexcept;
    [[nodiscard]] bool requested() const noexcept;

  private:
    volatile std::sig_atomic_t requested_{};
};

class ConsoleInterruptHandler final {
  public:
    explicit ConsoleInterruptHandler(ControlledInterrupt &interrupt) noexcept;
    // Spec 002 (RF-2.5): Ctrl+C asks the running request to stop, like the library token.
    explicit ConsoleInterruptHandler(CancelToken &token) noexcept;
    ~ConsoleInterruptHandler();
    ConsoleInterruptHandler(const ConsoleInterruptHandler &) = delete;
    ConsoleInterruptHandler &operator=(const ConsoleInterruptHandler &) = delete;

    [[nodiscard]] bool installed() const noexcept;

  private:
    void install() noexcept;

    using SignalHandler = void (*)(int);
    SignalHandler previous_{};
    bool installed_{};
};

} // namespace ayther::audio_qa
