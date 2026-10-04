#include "launcher_view.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <utility>

namespace ayther::replay_qa_launcher {
namespace {

constexpr DialogFilter any_file{"*", "*"};

constexpr std::array<audio_qa::CheckOptionCategory, 8> section_order{
    audio_qa::CheckOptionCategory::rom,          audio_qa::CheckOptionCategory::take,
    audio_qa::CheckOptionCategory::pack,         audio_qa::CheckOptionCategory::environment,
    audio_qa::CheckOptionCategory::auxiliary,    audio_qa::CheckOptionCategory::destination,
    audio_qa::CheckOptionCategory::presentation, audio_qa::CheckOptionCategory::language,
};

std::string invalid_code(const audio_qa::CheckOptionDescriptor &option) {
    return std::string{audio_qa::check_option_error_code(option.invalid_value_error)};
}

std::string phase_text(LauncherLanguage language, audio_qa::RequestPhaseKind kind) {
    using enum audio_qa::RequestPhaseKind;
    switch (kind) {
    case validating:
        return std::string{launcher_text(language, LauncherText::phase_validating)};
    case admitted:
        return std::string{launcher_text(language, LauncherText::phase_admitted)};
    case preparing:
        return std::string{launcher_text(language, LauncherText::phase_preparing)};
    case running:
        return std::string{launcher_text(language, LauncherText::phase_running)};
    case closing:
        return std::string{launcher_text(language, LauncherText::phase_closing)};
    case closed:
        return std::string{launcher_text(language, LauncherText::phase_closed)};
    }
    return {};
}

} // namespace

DialogSpec dialog_for(std::string_view flag) {
    if (flag == "--rom")
        return {FileDialog::open_file,
                {{"ROM", "md;bin;gen;smd;sms;gg;sfc;smc;nes;gb;gbc;gba;pce;zip;7z"}, any_file}};
    if (flag == "--take")
        return {FileDialog::open_files, {{"AYR", "ayr"}, any_file}};
    if (flag == "--pack")
        return {FileDialog::open_file, {{"AY", "ay"}, any_file}};
    if (flag == "--runtime")
        return {FileDialog::open_file, {{"EXE", "exe"}, any_file}};
    if (flag == "--core")
        return {FileDialog::open_file, {{"DLL", "dll"}, any_file}};
    if (flag == "--output")
        return {FileDialog::open_folder, {}};
    const auto *option = audio_qa::find_check_option(flag);
    if (option != nullptr && option->kind == audio_qa::CheckOptionKind::path)
        return {FileDialog::open_file, {any_file}};
    return {};
}

FieldInput input_for(audio_qa::CheckOptionKind kind) noexcept {
    switch (kind) {
    case audio_qa::CheckOptionKind::text:
        return FieldInput::text;
    case audio_qa::CheckOptionKind::path:
        return FieldInput::path;
    case audio_qa::CheckOptionKind::choice:
        return FieldInput::choice;
    case audio_qa::CheckOptionKind::unsigned_integer:
        return FieldInput::number;
    case audio_qa::CheckOptionKind::key_value:
        return FieldInput::key_value;
    }
    return FieldInput::text;
}

std::vector<LauncherSection> launcher_sections() {
    std::vector<LauncherSection> sections;
    for (const auto category : section_order) {
        LauncherSection section{category, {}};
        for (const auto &option : audio_qa::check_option_descriptors())
            if (option.category == category)
                section.fields.push_back({option.flag, option.launcher_field,
                                          input_for(option.kind), option.required,
                                          option.repeatable, option.values});
        sections.push_back(std::move(section));
    }
    return sections;
}

std::optional<std::string> local_issue(const audio_qa::CheckOptionDescriptor &option,
                                       std::string_view value) {
    if (value.empty())
        return std::nullopt;
    switch (option.kind) {
    case audio_qa::CheckOptionKind::choice:
        if (std::find(option.values.begin(), option.values.end(), value) == option.values.end())
            return invalid_code(option);
        break;
    case audio_qa::CheckOptionKind::unsigned_integer: {
        std::uint32_t number{};
        const auto *end = value.data() + value.size();
        const auto [last, error] = std::from_chars(value.data(), end, number);
        if (error != std::errc{} || last != end)
            return invalid_code(option);
        break;
    }
    case audio_qa::CheckOptionKind::key_value: {
        const auto equals = value.find('=');
        if (equals == std::string_view::npos || equals == 0U)
            return invalid_code(option);
        break;
    }
    case audio_qa::CheckOptionKind::text:
    case audio_qa::CheckOptionKind::path:
        break;
    }
    return std::nullopt;
}

std::optional<std::string> field_error(const LauncherModel &model, LauncherLanguage language,
                                       std::string_view flag) {
    const auto found =
        std::find_if(model.fields().begin(), model.fields().end(),
                     [flag](const ModelField &field) { return field.descriptor->flag == flag; });
    if (found == model.fields().end())
        return std::nullopt;
    const auto &option = *found->descriptor;
    if (option.repeatable) {
        for (const auto &value : found->values)
            if (auto issue = local_issue(option, value))
                return issue;
    } else if (auto issue = local_issue(option, found->value)) {
        return issue;
    }
    const auto missing = model.missing_required();
    if (std::any_of(missing.begin(), missing.end(),
                    [flag](const audio_qa::FieldIssue &issue) { return issue.field == flag; }))
        return std::string{launcher_text(language, LauncherText::required_missing)};
    if (auto issue = model.issue_of(flag))
        return issue;
    // The preflight may resolve from the environment what the launcher could not yet.
    if (model.source_of(option.launcher_field))
        return std::nullopt;
    return model.environment_issue(flag);
}

std::vector<SummaryRow> summary_rows(const LauncherModel &model, LauncherLanguage language) {
    std::vector<SummaryRow> rows;
    for (const auto &line : model.summary()) {
        const auto label = field_label(language, line.key);
        rows.push_back({line.key, label.empty() ? line.key : std::string{label},
                        line.no_pack ? std::string{launcher_text(language, LauncherText::no_pack)}
                                     : line.value,
                        std::string{source_text(language, line.source)}});
    }
    return rows;
}

ResultsView results_view(const LauncherModel &model, LauncherLanguage language) {
    ResultsView view;
    for (const auto &row : model.results()) {
        ResultRowView item{std::to_string(row.position + 1U),
                           row.take,
                           std::string{launcher_text(language, row.playback)},
                           std::string{launcher_text(language, row.traversal)},
                           std::string{launcher_text(language, row.evidence)},
                           {},
                           row.diagnostic};
        for (const auto &reason : row.reasons)
            item.reasons += (item.reasons.empty() ? "" : ", ") + reason;
        view.rows.push_back(std::move(item));
    }
    if (const auto joint = model.joint_result()) {
        view.joint = std::string{launcher_text(language, *joint)};
        view.note = std::string{launcher_text(language, LauncherText::not_a_visual_evaluation)};
    }
    return view;
}

std::string phase_line(const LauncherModel &model, LauncherLanguage language) {
    if (!model.active() && !model.joint_result())
        return std::string{launcher_text(language, LauncherText::phase_idle)};
    auto line = phase_text(language, model.phase().kind);
    if (const auto take = model.current_take())
        line += " · " + std::string{launcher_text(language, LauncherText::current_take)} + ' ' +
                std::to_string(*take + 1U);
    return line;
}

} // namespace ayther::replay_qa_launcher
