#include "console_interrupt.h"

#include <csignal>

namespace ayther::audio_qa {

namespace {

ControlledInterrupt *active_interrupt{};

extern "C" void handle_interrupt(int signal) noexcept {
    if (signal == SIGINT && active_interrupt != nullptr)
        active_interrupt->request();
}

} // namespace

void ControlledInterrupt::request() noexcept { requested_ = 1; }

bool ControlledInterrupt::requested() const noexcept { return requested_ != 0; }

ConsoleInterruptHandler::ConsoleInterruptHandler(ControlledInterrupt &interrupt) noexcept {
    if (active_interrupt != nullptr)
        return;
    active_interrupt = &interrupt;
    previous_ = std::signal(SIGINT, handle_interrupt);
    installed_ = previous_ != SIG_ERR;
    if (!installed_)
        active_interrupt = nullptr;
}

ConsoleInterruptHandler::~ConsoleInterruptHandler() {
    if (!installed_)
        return;
    (void)std::signal(SIGINT, previous_);
    active_interrupt = nullptr;
}

bool ConsoleInterruptHandler::installed() const noexcept { return installed_; }

} // namespace ayther::audio_qa
