#include "replay_duration_limit.h"

#include "recording_header.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_error(const qa::ReplayDurationLimit &result, const qa::ReplayDurationError expected,
                   const char *const code, const char *const message) {
    require(result.error == expected && qa::replay_duration_error_code(result.error) == code,
            message);
}

} // namespace

int main() {
    try {
        const auto ntsc_limit = qa::validate_replay_duration(qa::max_recording_frames, 60.0);
        require(ntsc_limit.error == qa::ReplayDurationError::none &&
                    ntsc_limit.duration_seconds == qa::max_replay_duration_seconds,
                "54000_frames_at_60_fps_were_not_accepted_as_900_seconds");

        const auto pal_limit = qa::validate_replay_duration(45'000U, 50.0);
        require(pal_limit.error == qa::ReplayDurationError::none &&
                    pal_limit.duration_seconds == qa::max_replay_duration_seconds,
                "45000_frames_at_50_fps_were_not_accepted_as_900_seconds");

        require_error(qa::validate_replay_duration(qa::max_recording_frames + 1U, 60.0),
                      qa::ReplayDurationError::frame_count_out_of_range,
                      "recording_frame_count_out_of_range", "frame_count_excess_was_not_rejected");
        require_error(qa::validate_replay_duration(45'001U, 50.0),
                      qa::ReplayDurationError::duration_too_long, "recording_duration_too_long",
                      "duration_excess_was_not_rejected");
        require_error(qa::validate_replay_duration(1U, 0.0),
                      qa::ReplayDurationError::invalid_declared_fps,
                      "recording_declared_fps_invalid", "zero_fps_was_not_rejected");
        require_error(qa::validate_replay_duration(1U, std::numeric_limits<double>::quiet_NaN()),
                      qa::ReplayDurationError::invalid_declared_fps,
                      "recording_declared_fps_invalid", "non_finite_fps_was_not_rejected");

        std::puts("replay_duration_limit_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "replay_duration_limit_test: %s\n", error.what());
        return 1;
    }
}
