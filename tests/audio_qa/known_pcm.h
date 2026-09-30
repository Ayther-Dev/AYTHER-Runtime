#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ayther::runtime::audio_qa {

inline constexpr std::string_view fixture_id = "known-pcm-v1";
inline constexpr int sample_rate = 44100;
inline constexpr std::size_t channels = 2;
inline constexpr std::size_t sample_frames = 1024;
using PcmBlock = std::array<std::int16_t, sample_frames * channels>;

// Controlled input only: indices describe this block, not captured device output.
[[nodiscard]] constexpr PcmBlock make_known_pcm() {
    PcmBlock pcm{};
    for (std::size_t frame = 4; frame < sample_frames; ++frame) {
        const auto index = static_cast<int>(frame);
        pcm[frame * channels] = static_cast<std::int16_t>(-8192 + 16 * index);
        pcm[frame * channels + 1] = static_cast<std::int16_t>(4096 - 4 * index);
    }
    constexpr std::array<std::int16_t, 8> marker{-32768, 32767, 0, -1, 8192, -4096, -16384, 16384};
    for (std::size_t index = 0; index < marker.size(); ++index) {
        pcm[index] = marker[index];
    }
    return pcm;
}

} // namespace ayther::runtime::audio_qa
