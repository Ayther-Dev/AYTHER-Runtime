#pragma once

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
    ~ConsoleInterruptHandler();
    ConsoleInterruptHandler(const ConsoleInterruptHandler &) = delete;
    ConsoleInterruptHandler &operator=(const ConsoleInterruptHandler &) = delete;

    [[nodiscard]] bool installed() const noexcept;

  private:
    using SignalHandler = void (*)(int);
    SignalHandler previous_{};
    bool installed_{};
};

} // namespace ayther::audio_qa
