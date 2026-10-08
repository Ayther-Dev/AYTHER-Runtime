#include "launcher_model.h"

#include <algorithm>
#include <utility>

namespace ayther::replay_qa_launcher {
namespace {

LauncherText playback_text(audio_qa::PlaybackKind kind) noexcept {
    switch (kind) {
    case audio_qa::PlaybackKind::natural_end:
        return LauncherText::playback_natural_end;
    case audio_qa::PlaybackKind::cancelled:
        return LauncherText::playback_cancelled;
    case audio_qa::PlaybackKind::failed:
        return LauncherText::playback_failed;
    case audio_qa::PlaybackKind::interrupted:
        return LauncherText::playback_interrupted;
    }
    return LauncherText::playback_failed;
}

LauncherText traversal_text(std::optional<audio_qa::TraversalKind> kind) noexcept {
    if (!kind)
        return LauncherText::traversal_unknown;
    switch (*kind) {
    case audio_qa::TraversalKind::linear:
        return LauncherText::traversal_linear;
    case audio_qa::TraversalKind::inspection:
        return LauncherText::traversal_inspection;
    case audio_qa::TraversalKind::post_end_inspection:
        return LauncherText::traversal_post_end_inspection;
    }
    return LauncherText::traversal_unknown;
}

// `--take[2]` belongs to the field `--take`.
bool issue_in_field(const audio_qa::FieldIssue &issue, std::string_view flag) {
    if (issue.field == flag)
        return true;
    return issue.field.size() > flag.size() && issue.field.starts_with(flag) &&
           issue.field[flag.size()] == '[';
}

// D-10: a path pasted with Windows "Copy as path" comes between double quotes.
std::string pasted_value(const audio_qa::CheckOptionDescriptor &descriptor, std::string value) {
    return descriptor.kind == audio_qa::CheckOptionKind::path ? unquoted_path(std::move(value))
                                                              : value;
}

} // namespace

std::string unquoted_path(std::string value) {
    if (value.size() >= 2U && value.front() == '"' && value.back() == '"')
        return value.substr(1U, value.size() - 2U);
    return value;
}

LauncherModel::LauncherModel() {
    for (const auto &descriptor : audio_qa::check_option_descriptors()) {
        ModelField field;
        field.descriptor = &descriptor;
        field.value = std::string{descriptor.default_value};
        fields_.push_back(std::move(field));
    }
    // plan §5.14: the launcher starts in visible presentation.
    if (auto *presentation = find("--presentation"))
        presentation->value = "visible";
}

ModelField *LauncherModel::find(std::string_view flag) {
    const auto found = std::find_if(fields_.begin(), fields_.end(), [flag](const ModelField &f) {
        return f.descriptor->flag == flag;
    });
    return found == fields_.end() ? nullptr : &*found;
}

const ModelField *LauncherModel::find(std::string_view flag) const {
    const auto found = std::find_if(fields_.begin(), fields_.end(), [flag](const ModelField &f) {
        return f.descriptor->flag == flag;
    });
    return found == fields_.end() ? nullptr : &*found;
}

std::string LauncherModel::value(std::string_view flag) const {
    const auto *field = find(flag);
    return field == nullptr ? std::string{} : field->value;
}

void LauncherModel::set(std::string_view flag, std::string value) {
    auto *field = find(flag);
    if (field == nullptr || field->descriptor->repeatable)
        return;
    field->value = pasted_value(*field->descriptor, std::move(value));
    dirty_ = true;
}

void LauncherModel::remove_pack() { set("--pack", {}); }

const std::vector<std::string> &LauncherModel::takes() const { return find("--take")->values; }

void LauncherModel::add_take(std::string path) {
    auto *takes = find("--take");
    takes->values.push_back(pasted_value(*takes->descriptor, std::move(path)));
    dirty_ = true;
}

void LauncherModel::remove_take(std::size_t position) {
    auto &takes = find("--take")->values;
    if (position >= takes.size())
        return;
    takes.erase(takes.begin() + static_cast<std::ptrdiff_t>(position));
    dirty_ = true;
}

void LauncherModel::move_take(std::size_t from, std::size_t to) {
    auto &takes = find("--take")->values;
    if (from >= takes.size() || to >= takes.size() || from == to)
        return;
    auto take = std::move(takes[from]);
    takes.erase(takes.begin() + static_cast<std::ptrdiff_t>(from));
    takes.insert(takes.begin() + static_cast<std::ptrdiff_t>(to), std::move(take));
    dirty_ = true;
}

void LauncherModel::repeat_take(std::size_t position) {
    auto &takes = find("--take")->values;
    if (position >= takes.size())
        return;
    takes.push_back(takes[position]);
    dirty_ = true;
}

void LauncherModel::set_values(std::string_view flag, std::vector<std::string> values) {
    auto *field = find(flag);
    if (field == nullptr || !field->descriptor->repeatable)
        return;
    for (auto &value : values)
        value = pasted_value(*field->descriptor, std::move(value));
    field->values = std::move(values);
    dirty_ = true;
}

void LauncherModel::set_environment(EnvironmentValues environment) {
    environment_ = std::move(environment);
    dirty_ = true;
}

bool LauncherModel::from_environment(std::string_view key) const {
    if (key == "runtime")
        return value("--runtime").empty() && environment_.runtime.has_value();
    if (key == "core")
        return value("--core").empty() && environment_.core.has_value();
    return false;
}

std::optional<std::string> LauncherModel::environment_issue(std::string_view flag) const {
    if (flag == "--core" && value("--core").empty() && !environment_.core)
        return std::string{"core_unresolved"};
    if (flag == "--runtime" && value("--runtime").empty() && !environment_.runtime)
        return std::string{"runtime_not_installed"};
    return std::nullopt;
}

audio_qa::ReplayRequest LauncherModel::request() const {
    audio_qa::ReplayRequest request;
    for (const auto &field : fields_) {
        const auto &descriptor = *field.descriptor;
        if (descriptor.text_field != nullptr)
            request.*(descriptor.text_field) = field.value;
        if (descriptor.list_field != nullptr)
            request.*(descriptor.list_field) = field.values;
    }
    if (from_environment("runtime"))
        request.runtime = *environment_.runtime;
    if (from_environment("core"))
        request.core = *environment_.core;
    return request;
}

std::vector<audio_qa::FieldIssue> LauncherModel::missing_required() const {
    std::vector<audio_qa::FieldIssue> missing;
    for (const auto &field : fields_) {
        const auto &descriptor = *field.descriptor;
        if (!descriptor.required)
            continue;
        const bool empty = descriptor.repeatable ? field.values.empty() : field.value.empty();
        const bool resolved = descriptor.flag == "--runtime" && from_environment("runtime");
        if (empty && !resolved)
            missing.push_back({std::string{descriptor.flag}, "missing_required_option"});
    }
    return missing;
}

void LauncherModel::revalidate(const Preflighter &preflight) {
    auto result = preflight(request());
    effective_ = std::move(result.effective);
    issues_ = std::move(result.issues);
    dirty_ = false;
}

std::optional<std::string> LauncherModel::issue_of(std::string_view flag) const {
    for (const auto &issue : issues_)
        if (issue_in_field(issue, flag))
            return issue.code;
    return std::nullopt;
}

std::optional<std::string> LauncherModel::take_issue(std::size_t position) const {
    const auto field = "--take[" + std::to_string(position) + "]";
    for (const auto &issue : issues_)
        if (issue.field == field)
            return issue.code;
    return std::nullopt;
}

std::vector<SummaryLine> LauncherModel::summary() const {
    std::vector<SummaryLine> lines;
    for (const auto &value : effective_) {
        SummaryLine line{value.key, value.value, value.source, false};
        if (from_environment(value.key))
            line.source = audio_qa::ValueSource::environment;
        line.no_pack = value.key == "pack" && value.value == "none";
        lines.push_back(std::move(line));
    }
    for (const auto *key : {"runtime", "core"}) {
        const bool present = std::any_of(lines.begin(), lines.end(),
                                         [key](const SummaryLine &l) { return l.key == key; });
        if (!present && from_environment(key))
            lines.push_back(
                {key, std::string{key} == "runtime" ? *environment_.runtime : *environment_.core,
                 audio_qa::ValueSource::environment, false});
    }
    return lines;
}

std::optional<audio_qa::ValueSource> LauncherModel::source_of(std::string_view key) const {
    for (const auto &line : summary())
        if (line.key == key)
            return line.source;
    if (from_environment(key))
        return audio_qa::ValueSource::environment;
    return std::nullopt;
}

bool LauncherModel::can_start() const {
    return !active_ && !dirty_ && missing_required().empty() && issues_.empty();
}

void LauncherModel::on_started() {
    active_ = true;
    phase_ = {audio_qa::RequestPhaseKind::validating, 0};
    current_take_.reset();
    results_.clear();
    joint_.reset();
    run_issues_.clear();
}

void LauncherModel::on_phase(audio_qa::RequestPhase phase) {
    phase_ = phase;
    if (phase.kind == audio_qa::RequestPhaseKind::preparing ||
        phase.kind == audio_qa::RequestPhaseKind::running)
        current_take_ = phase.take_position;
    if (phase.kind == audio_qa::RequestPhaseKind::closed) {
        active_ = false;
        current_take_.reset();
    }
}

void LauncherModel::on_finished(const audio_qa::RequestOutcome &outcome) {
    active_ = false;
    phase_ = {audio_qa::RequestPhaseKind::closed, 0};
    current_take_.reset();
    results_.clear();
    for (const auto &slot : outcome.per_take) {
        TakeResultRow row;
        if (const auto *take = std::get_if<audio_qa::TakeOutcome>(&slot)) {
            row.position = take->position;
            row.take = take->take;
            row.playback = playback_text(take->playback.kind);
            row.traversal = traversal_text(take->traversal);
            row.evidence = take->evidence.complete ? LauncherText::evidence_complete
                                                   : LauncherText::evidence_incomplete;
            row.reasons = take->evidence.reasons;
            row.diagnostic = take->playback.diagnostic;
        } else {
            const auto &pending = std::get<audio_qa::NotStartedTake>(slot);
            row.position = pending.position;
            row.take = pending.take;
            row.diagnostic = pending.reason;
        }
        results_.push_back(std::move(row));
    }
    run_issues_ = outcome.issues;
    joint_ = results_.empty() ? std::nullopt
             : outcome.linear_complete
                 ? std::optional<LauncherText>{LauncherText::joint_linear_complete}
                 : std::optional<LauncherText>{LauncherText::joint_not_linear_complete};
}

std::vector<std::string> apply_prefill(LauncherModel &model, std::string_view text) {
    struct Entry {
        std::string flag;
        std::string value;
        bool repeatable{};
    };
    std::vector<Entry> entries;
    std::vector<std::string> errors;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.empty())
            continue;
        const auto equals = line.find('=');
        const auto flag = line.substr(0, equals);
        const auto field =
            std::find_if(model.fields().begin(), model.fields().end(),
                         [flag](const ModelField &f) { return f.descriptor->flag == flag; });
        if (equals == std::string_view::npos || !flag.starts_with("--") ||
            field == model.fields().end()) {
            errors.emplace_back(line);
            continue;
        }
        entries.push_back({std::string{flag}, std::string{line.substr(equals + 1)},
                           field->descriptor->repeatable});
    }
    if (!errors.empty())
        return errors;
    std::vector<std::pair<std::string, std::vector<std::string>>> repeated;
    for (auto &entry : entries) {
        if (!entry.repeatable) {
            model.set(entry.flag, std::move(entry.value));
            continue;
        }
        const auto found =
            std::find_if(repeated.begin(), repeated.end(),
                         [&entry](const auto &group) { return group.first == entry.flag; });
        if (found == repeated.end())
            repeated.push_back({entry.flag, {std::move(entry.value)}});
        else
            found->second.push_back(std::move(entry.value));
    }
    for (auto &[flag, values] : repeated)
        model.set_values(flag, std::move(values));
    return errors;
}

} // namespace ayther::replay_qa_launcher
