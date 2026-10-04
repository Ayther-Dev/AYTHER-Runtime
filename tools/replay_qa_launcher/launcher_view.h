#pragma once

#include "check_option_descriptors.h"
#include "launcher_messages.h"
#include "launcher_model.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ayther::replay_qa_launcher {

// Spec 002, plan §5.14: what the window draws, derived from the model without GPU. The
// window only lays these values out with Dear ImGui and opens the SDL3 dialogs.

// BR-166 (RF-1.1): ROM, takes and pack are chosen with the SDL3 file dialogs.
enum class FileDialog { none, open_file, open_files, open_folder };

struct DialogFilter {
    std::string_view name;
    // SDL3 format: extensions separated by `;`, or `*`.
    std::string_view pattern;
};

struct DialogSpec {
    FileDialog kind{FileDialog::none};
    std::vector<DialogFilter> filters;
};

[[nodiscard]] DialogSpec dialog_for(std::string_view flag);

// BR-167 (RF-1.5): one section per category, with every option of the table once.
enum class FieldInput { text, path, choice, number, key_value };

[[nodiscard]] FieldInput input_for(audio_qa::CheckOptionKind kind) noexcept;

struct LauncherSectionField {
    std::string_view flag;
    std::string_view launcher_field;
    FieldInput input{FieldInput::text};
    bool required{};
    bool repeatable{};
    std::span<const std::string_view> values;
};

struct LauncherSection {
    audio_qa::CheckOptionCategory category{audio_qa::CheckOptionCategory::rom};
    std::vector<LauncherSectionField> fields;
};

[[nodiscard]] std::vector<LauncherSection> launcher_sections();

// The validation of the kind of the option, before the preflight: the code of the parser.
[[nodiscard]] std::optional<std::string> local_issue(const audio_qa::CheckOptionDescriptor &option,
                                                     std::string_view value);

// BR-167, BR-168 (RF-1.2, RF-1.5, RF-1.7): the error shown under a field. The validation of
// the kind first, then a missing required value, then the result of the preflight, then a
// value the environment could not resolve.
[[nodiscard]] std::optional<std::string>
field_error(const LauncherModel &model, LauncherLanguage language, std::string_view flag);

// BR-168 (RF-1.3, RF-1.6): the effective summary, with «Sin pack» and the origin.
struct SummaryRow {
    std::string key;
    std::string label;
    std::string value;
    std::string source;
};

[[nodiscard]] std::vector<SummaryRow> summary_rows(const LauncherModel &model,
                                                   LauncherLanguage language);

// BR-170 (RF-2.4, RF-2.12): the results of the last request, localized.
struct ResultRowView {
    // From 1, as the user counts the takes.
    std::string position;
    std::string take;
    std::string playback;
    std::string traversal;
    std::string evidence;
    std::string reasons;
    std::string diagnostic;
};

struct ResultsView {
    std::vector<ResultRowView> rows;
    // Empty until a request finishes.
    std::string joint;
    std::string note;
};

[[nodiscard]] ResultsView results_view(const LauncherModel &model, LauncherLanguage language);

// RF-2.3: the phase of the request and the take in progress, from 1.
[[nodiscard]] std::string phase_line(const LauncherModel &model, LauncherLanguage language);

} // namespace ayther::replay_qa_launcher
