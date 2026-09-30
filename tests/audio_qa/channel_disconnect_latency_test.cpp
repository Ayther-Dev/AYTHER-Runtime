#include "inherited_channel.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <variant>

namespace qa = ayther::audio_qa;
namespace {

constexpr auto disconnect_notification_budget = std::chrono::milliseconds{10};
constexpr std::size_t repetitions = 100U;

void require(const bool condition, const char *const message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct Event final {
    HANDLE value{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    ~Event() {
        if (value != nullptr)
            (void)CloseHandle(value);
    }
};

} // namespace

int main() {
    try {
        std::chrono::nanoseconds maximum_latency{};
        for (std::size_t repetition{}; repetition < repetitions; ++repetition) {
            auto created = qa::create_inherited_data_channel();
            auto *channel = std::get_if<qa::InheritedDataChannel>(&created);
            require(channel != nullptr, "channel_creation_failed");
            Event reader_ready;
            Event reader_finished;
            require(reader_ready.value != nullptr && reader_finished.value != nullptr,
                    "event_creation_failed");

            qa::ChannelReadResult result{qa::ChannelReadResult::failed};
            auto finished_at = std::chrono::steady_clock::time_point{};
            std::thread reader{[&] {
                std::array<std::byte, 1> bytes{};
                std::size_t received{};
                (void)SetEvent(reader_ready.value);
                result = qa::read_channel(channel->supervisor_read, bytes, received);
                finished_at = std::chrono::steady_clock::now();
                (void)SetEvent(reader_finished.value);
            }};
            require(WaitForSingleObject(reader_ready.value, 1000) == WAIT_OBJECT_0,
                    "reader_did_not_start");
            const auto disconnected_at = std::chrono::steady_clock::now();
            channel->runtime_write.reset();
            const auto wait_result = WaitForSingleObject(
                reader_finished.value, static_cast<DWORD>(disconnect_notification_budget.count()));
            reader.join();
            require(wait_result == WAIT_OBJECT_0, "channel_break_notification_exceeded_budget");
            require(result == qa::ChannelReadResult::end_of_stream,
                    "channel_break_was_not_reported_as_end_of_stream");
            const auto latency = finished_at - disconnected_at;
            require(latency >= std::chrono::steady_clock::duration::zero() &&
                        latency <= disconnect_notification_budget,
                    "channel_break_latency_outside_budget");
            maximum_latency =
                (std::max)(maximum_latency,
                           std::chrono::duration_cast<std::chrono::nanoseconds>(latency));
        }

        std::printf(
            "channel_disconnect_latency_test: repetitions=%zu max_latency_ns=%llu "
            "budget_ns=%llu result=passed\n",
            repetitions, static_cast<unsigned long long>(maximum_latency.count()),
            static_cast<unsigned long long>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(disconnect_notification_budget)
                    .count()));
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "channel_disconnect_latency_test: %s\n", error.what());
        return 1;
    }
}
