#pragma once

#include "check_option_descriptors.h"
#include "effective_values.h"

#include <span>
#include <string_view>

namespace ayther::replay_qa_launcher {

// Spec 002 (RNF-7; plan §5.14): the texts of the launcher, Spanish by default.
enum class LauncherLanguage { spanish, english };

enum class LauncherText {
    window_title,
    start,
    cancel,
    remove_pack,
    choose_file,
    add_take,
    remove_take,
    move_up,
    move_down,
    repeat_take,
    no_pack,
    no_takes,
    summary_title,
    issues_title,
    results_title,
    required_missing,
    phase_idle,
    phase_validating,
    phase_admitted,
    phase_preparing,
    phase_running,
    phase_closing,
    phase_closed,
    current_take,
    playback,
    traversal,
    evidence,
    playback_natural_end,
    playback_cancelled,
    playback_failed,
    playback_interrupted,
    playback_not_started,
    traversal_linear,
    traversal_inspection,
    traversal_post_end_inspection,
    traversal_unknown,
    evidence_complete,
    evidence_incomplete,
    joint_linear_complete,
    joint_not_linear_complete,
    not_a_visual_evaluation,
};

[[nodiscard]] std::span<const LauncherText> all_launcher_texts() noexcept;
[[nodiscard]] std::string_view launcher_text(LauncherLanguage language, LauncherText text) noexcept;
// The label of the field of an option, by its `launcher_field`.
[[nodiscard]] std::string_view field_label(LauncherLanguage language,
                                           std::string_view launcher_field) noexcept;
[[nodiscard]] std::string_view section_title(LauncherLanguage language,
                                             audio_qa::CheckOptionCategory category) noexcept;
[[nodiscard]] std::string_view source_text(LauncherLanguage language,
                                           audio_qa::ValueSource source) noexcept;
[[nodiscard]] LauncherLanguage parse_launcher_language(std::string_view code) noexcept;

} // namespace ayther::replay_qa_launcher
