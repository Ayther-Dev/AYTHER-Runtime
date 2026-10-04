#include "console_interrupt.h"

#include <csignal>

namespace ayther::audio_qa {

namespace {

ControlledInterrupt *active_interrupt{};
CancelToken *active_token{};

extern "C" void handle_interrupt(int signal) noexcept {
    if (signal != SIGINT)
        return;
    if (active_interrupt != nullptr)
        active_interrupt->request();
    if (active_token != nullptr)
        active_token->request();
}

} // namespace

void ControlledInterrupt::request() noexcept { requested_ = 1; }

bool ControlledInterrupt::requested() const noexcept { return requested_ != 0; }

ConsoleInterruptHandler::ConsoleInterruptHandler(ControlledInterrupt &interrupt) noexcept {
    if (active_interrupt != nullptr || active_token != nullptr)
        return;
    active_interrupt = &interrupt;
    install();
}

ConsoleInterruptHandler::ConsoleInterruptHandler(CancelToken &token) noexcept {
    if (active_interrupt != nullptr || active_token != nullptr)
        return;
    active_token = &token;
    install();
}

void ConsoleInterruptHandler::install() noexcept {
    previous_ = std::signal(SIGINT, handle_interrupt);
    installed_ = previous_ != SIG_ERR;
    if (!installed_) {
        active_interrupt = nullptr;
        active_token = nullptr;
    }
}

ConsoleInterruptHandler::~ConsoleInterruptHandler() {
    if (!installed_)
        return;
    (void)std::signal(SIGINT, previous_);
    active_interrupt = nullptr;
    active_token = nullptr;
}

bool ConsoleInterruptHandler::installed() const noexcept { return installed_; }

} // namespace ayther::audio_qa
