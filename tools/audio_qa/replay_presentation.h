#pragma once

#include <cstdint>
#include <string>

namespace ayther::audio_qa {

struct ReplayPresentation {
    std::string mode = "none";
    std::string code = "not_requested";
    std::string output_profile;
    std::string audio_backend;
    std::uint32_t presented_frames{};
    std::uint32_t affected_frames{};
    std::uint32_t first_affected_frame{};
    std::uint32_t last_affected_frame{};
    bool cancelled{};
    std::uint32_t initial_width{};
    std::uint32_t initial_height{};
    bool hd_enabled{};
    bool shaders_enabled{};

    void affect(const std::uint32_t frame) noexcept {
        if (affected_frames != 0U && last_affected_frame == frame)
            return;
        if (affected_frames == 0U)
            first_affected_frame = frame;
        last_affected_frame = frame;
        ++affected_frames;
    }
    [[nodiscard]] bool complete() const noexcept {
        return mode == "none" || (code == "presented" && affected_frames == 0U && !cancelled);
    }
    bool operator==(const ReplayPresentation &) const = default;
};

} // namespace ayther::audio_qa
