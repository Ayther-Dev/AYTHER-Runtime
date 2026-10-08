#include "channel_pump.h"

#include <chrono>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ayther::audio_qa {

namespace {

[[nodiscard]] std::size_t bytes_of(const ProtocolReadResult &result) noexcept {
    const auto *bytes = std::get_if<std::vector<std::byte>>(&result);
    return bytes != nullptr ? bytes->size() : 0U;
}

} // namespace

ChannelPump::ChannelPump(Reader reader, const std::size_t max_queued_bytes, Interrupt interrupt)
    : reader_(std::move(reader)), max_queued_bytes_(max_queued_bytes),
      interrupt_(std::move(interrupt)), thread_([this] { run(); }) {}

ChannelPump::~ChannelPump() {
    {
        const std::lock_guard lock{mutex_};
        stop_ = true;
    }
    changed_.notify_all();
    // A read may start between the stop check and the cancellation: interrupt until the
    // reader thread has finished.
    while (!finished_.load(std::memory_order_acquire)) {
        if (interrupt_) {
            interrupt_();
        } else {
#if defined(_WIN32)
            (void)CancelSynchronousIo(static_cast<HANDLE>(thread_.native_handle()));
#endif
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    thread_.join();
}

void ChannelPump::run() {
    for (;;) {
        {
            std::unique_lock lock{mutex_};
            changed_.wait(lock, [this] { return stop_ || queued_bytes_ < max_queued_bytes_; });
            if (stop_)
                break;
        }
        auto result = reader_();
        const bool error = std::holds_alternative<ProtocolIoError>(result);
        {
            const std::lock_guard lock{mutex_};
            queued_bytes_ += bytes_of(result);
            queue_.push_back(std::move(result));
            if (error)
                ended_ = true;
        }
        changed_.notify_all();
        if (error)
            break;
    }
    finished_.store(true, std::memory_order_release);
}

ProtocolReadResult ChannelPump::pop() {
    std::unique_lock lock{mutex_};
    changed_.wait(lock, [this] { return !queue_.empty() || stop_; });
    if (queue_.empty())
        return ProtocolIoError::read_failed;
    auto result = std::move(queue_.front());
    queue_.pop_front();
    queued_bytes_ -= bytes_of(result);
    // After the reader's error, later pops keep returning it.
    if (queue_.empty() && ended_ && std::holds_alternative<ProtocolIoError>(result))
        queue_.push_back(result);
    changed_.notify_all();
    return result;
}

} // namespace ayther::audio_qa
