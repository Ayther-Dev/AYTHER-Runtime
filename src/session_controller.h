#pragma once

#include <ayther/ayther_session.h>

#include <memory>
#include <utility>

namespace ayther::runtime {

/// Owns the Engine session boundary independently from UI/presentation state.
class SessionController final {
public:
    explicit SessionController(
        std::unique_ptr<ayther::AytherSession> session) noexcept
        : session_(std::move(session)) {}

    SessionController(const SessionController&) = delete;
    SessionController& operator=(const SessionController&) = delete;
    SessionController(SessionController&&) noexcept = default;
    SessionController& operator=(SessionController&&) noexcept = default;

    [[nodiscard]] explicit operator bool() const noexcept {
        return session_ != nullptr;
    }
    [[nodiscard]] ayther::AytherSession* get() noexcept { return session_.get(); }
    [[nodiscard]] const ayther::AytherSession* get() const noexcept {
        return session_.get();
    }
    [[nodiscard]] ayther::AytherSession* operator->() noexcept {
        return session_.get();
    }
    [[nodiscard]] const ayther::AytherSession* operator->() const noexcept {
        return session_.get();
    }
    void reset() noexcept { session_.reset(); }

private:
    std::unique_ptr<ayther::AytherSession> session_;
};

}  // namespace ayther::runtime
