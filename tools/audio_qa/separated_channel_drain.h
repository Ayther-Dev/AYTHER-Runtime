#pragma once

#include "inherited_channel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace ayther::audio_qa {

enum class DrainedChannel : std::size_t { facts, pcm, logs };

struct ChannelDrainResult {
    std::vector<std::byte> bytes;
    bool overflow{};
    bool read_failed{};
};

class SeparatedChannelDrain final {
  public:
    SeparatedChannelDrain(OwnedChannelHandle facts, OwnedChannelHandle pcm, OwnedChannelHandle logs,
                          std::array<std::size_t, 3> limits) noexcept;
    ~SeparatedChannelDrain();
    SeparatedChannelDrain(const SeparatedChannelDrain &) = delete;
    SeparatedChannelDrain &operator=(const SeparatedChannelDrain &) = delete;

    [[nodiscard]] bool start();
    void join() noexcept;
    [[nodiscard]] const ChannelDrainResult &result(DrainedChannel channel) const noexcept;

  private:
    static void drain(OwnedChannelHandle &channel, std::size_t limit,
                      ChannelDrainResult &result) noexcept;

    std::array<OwnedChannelHandle, 3> channels_;
    std::array<std::size_t, 3> limits_{};
    std::array<ChannelDrainResult, 3> results_;
    std::array<std::thread, 3> threads_;
    bool started_{};
};

} // namespace ayther::audio_qa
