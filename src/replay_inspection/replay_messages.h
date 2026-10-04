#pragma once

#include <span>
#include <string_view>

namespace ayther::replay_inspection {

// Spec 002 (RNF-7, RNF-4): the texts of the replay window, Spanish by default.
enum class ReplayLanguage { spanish, english };

enum class ReplayMessage {
    phase_preparing,
    phase_playing,
    phase_pausing,
    phase_paused,
    phase_recovering,
    phase_interrupted,
    phase_ended,
    phase_closing,
    busy,
    not_available,
    no_next_frame,
    recovery_failed,
    recovering,
    cause_video_lost,
    cause_audio_device_removed,
    cause_window_minimized,
    cause_device_lost,
    cause_unknown,
    no_pack,
    label_rom,
    label_take,
    label_pack,
    label_phase,
    label_frame,
    label_total_frames,
    label_elapsed,
    label_remaining,
    label_processing,
    label_fps,
    label_occurrences,
    label_overflow,
    debug_hidden_hint,
};

[[nodiscard]] std::span<const ReplayMessage> all_replay_messages() noexcept;
[[nodiscard]] std::string_view replay_message(ReplayLanguage language,
                                              ReplayMessage message) noexcept;
[[nodiscard]] ReplayLanguage parse_replay_language(std::string_view code) noexcept;
// The message of an interruption cause of presentation_health.
[[nodiscard]] ReplayMessage interruption_cause_message(std::string_view cause) noexcept;

} // namespace ayther::replay_inspection
