#include "separated_channel_drain.h"

#include <algorithm>
#include <functional>
#include <span>
#include <utility>

namespace ayther::audio_qa {

SeparatedChannelDrain::SeparatedChannelDrain(OwnedChannelHandle facts, OwnedChannelHandle pcm,
                                             OwnedChannelHandle logs,
                                             const std::array<std::size_t, 3> limits) noexcept
    : channels_{std::move(facts), std::move(pcm), std::move(logs)}, limits_(limits) {}

SeparatedChannelDrain::~SeparatedChannelDrain() { join(); }

bool SeparatedChannelDrain::start() {
    if (started_ ||
        std::any_of(channels_.begin(), channels_.end(),
                    [](const auto &channel) { return !channel; }) ||
        std::any_of(limits_.begin(), limits_.end(), [](const auto limit) { return limit == 0; })) {
        return false;
    }
    for (std::size_t index = 0; index < channels_.size(); ++index) {
        results_[index].bytes.reserve(limits_[index]);
        threads_[index] = std::thread{&SeparatedChannelDrain::drain, std::ref(channels_[index]),
                                      limits_[index], std::ref(results_[index])};
    }
    started_ = true;
    return true;
}

void SeparatedChannelDrain::join() noexcept {
    for (auto &thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

const ChannelDrainResult &
SeparatedChannelDrain::result(const DrainedChannel channel) const noexcept {
    return results_[static_cast<std::size_t>(channel)];
}

void SeparatedChannelDrain::drain(OwnedChannelHandle &channel, const std::size_t limit,
                                  ChannelDrainResult &result) noexcept {
    std::array<std::byte, 4096> buffer{};
    while (true) {
        std::size_t received{};
        const auto status = read_channel(channel, std::span{buffer}, received);
        if (status == ChannelReadResult::end_of_stream) {
            return;
        }
        if (status != ChannelReadResult::data || received == 0) {
            result.read_failed = true;
            return;
        }
        const auto available = limit - result.bytes.size();
        const auto retained = (std::min)(available, received);
        result.bytes.insert(result.bytes.end(), buffer.begin(),
                            buffer.begin() + static_cast<std::ptrdiff_t>(retained));
        result.overflow = result.overflow || retained != received;
    }
}

} // namespace ayther::audio_qa
