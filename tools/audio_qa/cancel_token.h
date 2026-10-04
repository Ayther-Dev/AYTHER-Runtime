#pragma once

#include <atomic>

namespace ayther::audio_qa {

// Spec 002 (contracts.md C5, RF-2.5): asks a running request to stop. Safe between
// threads and from the console interrupt handler: the flag is a lock-free atomic.
class CancelToken {
  public:
    void request() noexcept { requested_.store(true, std::memory_order_release); }
    [[nodiscard]] bool requested() const noexcept {
        return requested_.load(std::memory_order_acquire);
    }

  private:
    std::atomic<bool> requested_{};
    static_assert(std::atomic<bool>::is_always_lock_free);
};

} // namespace ayther::audio_qa
