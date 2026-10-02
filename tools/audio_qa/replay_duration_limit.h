#pragma once

#include <cstdint>
#include <string_view>

namespace ayther::audio_qa {

inline constexpr double max_replay_duration_seconds = 900.0;

enum class ReplayDurationError {
    none,
    frame_count_out_of_range,
    invalid_declared_fps,
    duration_too_long,
};

struct ReplayDurationLimit {
    ReplayDurationError error{ReplayDurationError::none};
    double duration_seconds{};
};

[[nodiscard]] ReplayDurationLimit validate_replay_duration(std::uint32_t frame_count,
                                                           double declared_fps) noexcept;

[[nodiscard]] std::string_view replay_duration_error_code(ReplayDurationError error) noexcept;

} // namespace ayther::audio_qa
