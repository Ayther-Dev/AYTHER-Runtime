#pragma once

#include "protocol_io.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace ayther::audio_qa {

// Spec 002 (RF-4.6, D-6b): reads protocol messages on its own thread into a bounded queue, so
// the consumer may persist each one durably without stalling the producer at the other end of
// the channel. The first error ends the reading; the consumer receives it after the messages.
class ChannelPump final {
  public:
    using Reader = std::function<ProtocolReadResult()>;
    // Unblocks a read in progress when the pump closes early. Without it, Windows cancels the
    // reader thread's synchronous I/O.
    using Interrupt = std::function<void()>;

    ChannelPump(Reader reader, std::size_t max_queued_bytes, Interrupt interrupt = {});
    ~ChannelPump();
    ChannelPump(const ChannelPump &) = delete;
    ChannelPump &operator=(const ChannelPump &) = delete;

    // The next message in order, waiting for it; after the reader's error, that error.
    [[nodiscard]] ProtocolReadResult pop();

  private:
    void run();

    Reader reader_;
    std::size_t max_queued_bytes_;
    Interrupt interrupt_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<ProtocolReadResult> queue_;
    std::size_t queued_bytes_{};
    bool stop_{};
    bool ended_{};
    std::atomic<bool> finished_{};
    std::thread thread_;
};

} // namespace ayther::audio_qa
