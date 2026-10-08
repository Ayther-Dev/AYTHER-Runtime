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
    // DI-25: the late frames recorded and whether any delay exceeded the tolerance. Not part
    // of the terminal.
    std::uint32_t late_frames_{};
    bool intolerable_{};

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
        if (!cause.empty())
            intolerable_ = true;
        if (code == "presented" && !cause.empty())
            code = cause;
    }
    // DI-25 (D-6b): a frame of continuous playback that completed `periods_late` periods past
    // its deadline. It is recorded as a degraded cadence; while each such delay is at most 3
    // periods and there is at most one per 1000 frames of the take (at least one), the
    // observation stays complete.
    void late(const std::uint32_t frame, const double periods_late,
              const std::uint32_t take_frames) noexcept {
        affect(frame);
        if (code == "presented")
            code = "cadence_degraded";
        ++late_frames_;
        const std::uint32_t budget = (std::max)(1U, take_frames / 1000U);
        if (periods_late > 3.0 || late_frames_ > budget)
            intolerable_ = true;
    }
    // DI-25: the tolerated late frames, part of the terminal (0 when a delay exceeded the
    // tolerance); a supervisor restores them to check the result.
    [[nodiscard]] std::uint32_t tolerated_late_frames() const noexcept {
        return intolerable_ ? 0U : late_frames_;
    }
    void restore_tolerated_late_frames(const std::uint32_t count) noexcept {
        late_frames_ = count;
        intolerable_ = false;
    }
    [[nodiscard]] bool complete() const noexcept {
        if (mode == "none")
            return true;
        if (cancelled)
            return false;
        if (code == "presented" && affected_frames == 0U)
            return true;
        return code == "cadence_degraded" && !intolerable_ && affected_frames <= late_frames_;
    }
    // The terminal fields; what was counted is not part of the value.
    bool operator==(const ReplayPresentation &other) const noexcept {
        return mode == other.mode && code == other.code && output_profile == other.output_profile &&
               audio_backend == other.audio_backend && presented_frames == other.presented_frames &&
               affected_frames == other.affected_frames &&
               first_affected_frame == other.first_affected_frame &&
               last_affected_frame == other.last_affected_frame && cancelled == other.cancelled &&
               initial_width == other.initial_width && initial_height == other.initial_height &&
               hd_enabled == other.hd_enabled && shaders_enabled == other.shaders_enabled &&
               tolerated_late_frames() == other.tolerated_late_frames();
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
