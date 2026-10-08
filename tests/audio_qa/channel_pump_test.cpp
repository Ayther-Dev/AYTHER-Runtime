// Spec 002 (RF-4.6, D-6b; campaign 2026-10-07): the supervisor persisted each evidence
// fragment durably in the same loop that read the Runtime's channel. A slow flush filled the
// pipe and stalled the Runtime, its audio thread included, for up to 890 ms. ChannelPump reads
// the channel on its own thread into a bounded queue so persistence never blocks the producer.
#include "channel_pump.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

std::vector<std::byte> message(const std::size_t bytes, const std::byte value) {
    return std::vector<std::byte>(bytes, value);
}

} // namespace

int main() {
    using namespace std::chrono_literals;

    // A slow consumer does not slow the reader: every message is read while the consumer
    // has not taken any.
    {
        std::atomic<int> produced{};
        qa::ChannelPump pump{[&produced]() -> qa::ProtocolReadResult {
                                 const int index = produced.fetch_add(1);
                                 if (index == 100)
                                     return qa::ProtocolIoError::unexpected_end;
                                 return message(1024, static_cast<std::byte>(index));
                             },
                             1U << 20U};
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (produced.load() <= 100 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
        expect(produced.load() == 101,
               "D-6b: the reader drains the channel while the consumer is busy");
        bool ordered = true;
        for (int index = 0; index < 100; ++index) {
            const auto next = pump.pop();
            const auto *bytes = std::get_if<std::vector<std::byte>>(&next);
            ordered = ordered && bytes != nullptr && bytes->size() == 1024U &&
                      bytes->front() == static_cast<std::byte>(index);
        }
        expect(ordered, "D-6b: messages come out complete and in order");
        const auto end = pump.pop();
        expect(std::holds_alternative<qa::ProtocolIoError>(end) &&
                   std::get<qa::ProtocolIoError>(end) == qa::ProtocolIoError::unexpected_end,
               "D-6b: the reader's final error reaches the consumer after the messages");
    }

    // The queue is bounded: above its byte limit the reader waits for the consumer.
    {
        std::atomic<int> produced{};
        qa::ChannelPump pump{[&produced]() -> qa::ProtocolReadResult {
                                 produced.fetch_add(1);
                                 return message(4096, std::byte{1});
                             },
                             16U * 1024U};
        std::this_thread::sleep_for(200ms);
        expect(produced.load() <= 6,
               "D-6b: the queue stops reading at its byte limit (bounded memory)");
        (void)pump.pop();
        (void)pump.pop();
        std::this_thread::sleep_for(100ms);
        expect(produced.load() > 4, "D-6b: the reader resumes once the consumer takes some");
    }

    // Leaving early (an invalid stream while the Runtime still runs) must not hang on a read
    // that never returns: the interrupt hook unblocks it and the destructor joins.
    {
        std::atomic<bool> interrupted{};
        std::atomic<bool> reading{};
        const auto started = std::chrono::steady_clock::now();
        {
            qa::ChannelPump pump{[&]() -> qa::ProtocolReadResult {
                                     reading = true;
                                     while (!interrupted.load())
                                         std::this_thread::sleep_for(1ms);
                                     return qa::ProtocolIoError::read_failed;
                                 },
                                 1U << 20U, [&interrupted]() { interrupted = true; }};
            while (!reading.load())
                std::this_thread::sleep_for(1ms);
        }
        expect(interrupted.load() && std::chrono::steady_clock::now() - started < 2s,
               "D-6b: closing the pump interrupts a blocked read and joins its thread");
    }

    if (failures != 0)
        return 1;
    std::cout << "channel pump: reads ahead of a slow consumer, bounded\n";
    return 0;
}
