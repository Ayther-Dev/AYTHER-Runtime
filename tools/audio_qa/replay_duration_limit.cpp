#include "replay_duration_limit.h"

#include "recording_header.h"

#include <cmath>

namespace ayther::audio_qa {

ReplayDurationLimit validate_replay_duration(const std::uint32_t frame_count,
                                             const double declared_fps) noexcept {
    if (frame_count < min_recording_frames || frame_count > max_recording_frames) {
        return {ReplayDurationError::frame_count_out_of_range, 0.0};
    }
    if (!std::isfinite(declared_fps) || declared_fps <= 0.0) {
        return {ReplayDurationError::invalid_declared_fps, 0.0};
    }
    const double duration = static_cast<double>(frame_count) / declared_fps;
    return {duration > max_replay_duration_seconds ? ReplayDurationError::duration_too_long
                                                   : ReplayDurationError::none,
            duration};
}

std::string_view replay_duration_error_code(const ReplayDurationError error) noexcept {
    switch (error) {
    case ReplayDurationError::none:
        return "none";
    case ReplayDurationError::frame_count_out_of_range:
        return "recording_frame_count_out_of_range";
    case ReplayDurationError::invalid_declared_fps:
        return "recording_declared_fps_invalid";
    case ReplayDurationError::duration_too_long:
        return "recording_duration_too_long";
    }
    return "unknown_replay_duration_error";
}

} // namespace ayther::audio_qa
