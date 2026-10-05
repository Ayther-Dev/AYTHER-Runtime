#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

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
    // D-8 (campaign 2026-10-04): the take frames already counted. They are not part of the
    // terminal: a frame presented again after an inspection, or affected again, counts once.
    std::vector<bool> presented_seen;
    std::vector<bool> affected_seen;

    // A take frame presented in playback; each frame counts once, so the presented frames never
    // exceed the frames consumed.
    void presented(const std::uint32_t frame) noexcept {
        if (first_time(presented_seen, frame))
            ++presented_frames;
    }
    // A take frame whose presentation was affected. The range spans every affected frame, in
    // whatever order they were visited, and each frame counts once.
    void affect(const std::uint32_t frame) noexcept {
        if (affected_frames == 0U) {
            first_affected_frame = frame;
            last_affected_frame = frame;
        } else {
            first_affected_frame = (std::min)(first_affected_frame, frame);
            last_affected_frame = (std::max)(last_affected_frame, frame);
        }
        if (first_time(affected_seen, frame))
            ++affected_frames;
    }
    // D-14 (campaign 2026-10-05; RF-2.6, plan §5.10 step 3): an interruption of the
    // presentation (minimized window, audio device removed, video lost), even if it recovers,
    // is an incomplete audiovisual observation. The presentation is no longer complete and
    // the interruption is its reason; the first problem keeps naming it, as with a degraded
    // frame. The playback and the frames presented are not touched.
    void interrupted(const std::string &cause) {
        if (code == "presented" && !cause.empty())
            code = cause;
    }
    [[nodiscard]] bool complete() const noexcept {
        return mode == "none" || (code == "presented" && affected_frames == 0U && !cancelled);
    }
    // The terminal fields; what was counted is not part of the value.
    bool operator==(const ReplayPresentation &other) const noexcept {
        return mode == other.mode && code == other.code && output_profile == other.output_profile &&
               audio_backend == other.audio_backend && presented_frames == other.presented_frames &&
               affected_frames == other.affected_frames &&
               first_affected_frame == other.first_affected_frame &&
               last_affected_frame == other.last_affected_frame && cancelled == other.cancelled &&
               initial_width == other.initial_width && initial_height == other.initial_height &&
               hd_enabled == other.hd_enabled && shaders_enabled == other.shaders_enabled;
    }

  private:
    [[nodiscard]] static bool first_time(std::vector<bool> &seen,
                                         const std::uint32_t frame) noexcept {
        try {
            if (frame >= seen.size())
                seen.resize(static_cast<std::size_t>(frame) + 1U, false);
            if (seen[frame])
                return false;
            seen[frame] = true;
            return true;
        } catch (...) {
            // Without memory to remember it, the frame is counted as new.
            return true;
        }
    }
};

} // namespace ayther::audio_qa
