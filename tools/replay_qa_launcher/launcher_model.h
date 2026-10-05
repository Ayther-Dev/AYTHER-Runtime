#pragma once

#include "check_runner.h"
#include "environment_resolver.h"
#include "launcher_messages.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ayther::replay_qa_launcher {

// Spec 002, plan §5.14 (RF-1, RF-2): the pure model of the launcher form. One field per
// option of the table; it builds the ReplayRequest, keeps the result of the preflight per
// field, knows the phase and the take in progress, and enables Start and Cancel.
struct ModelField {
    const audio_qa::CheckOptionDescriptor *descriptor{};
    std::string value;
    // Repeatable options: takes and core options, in order.
    std::vector<std::string> values;
};

struct SummaryLine {
    std::string key;
    std::string value;
    audio_qa::ValueSource source{audio_qa::ValueSource::explicit_option};
    // RF-1.3: the pack value `none` is shown as «Sin pack».
    bool no_pack{};
};

struct TakeResultRow {
    std::size_t position{};
    std::string take;
    LauncherText playback{LauncherText::playback_not_started};
    LauncherText traversal{LauncherText::traversal_unknown};
    LauncherText evidence{LauncherText::evidence_incomplete};
    std::vector<std::string> reasons;
    std::string diagnostic;
};

// D-10 (RF-1.6): a path pasted between one pair of double quotes (Windows "Copy as path") is
// the path without them; anything else is kept as typed. Every path field goes through it, so
// the effective value shown and the one recorded are the path itself.
[[nodiscard]] std::string unquoted_path(std::string value);

using Preflighter = std::function<audio_qa::PreflightResult(const audio_qa::ReplayRequest &)>;

class LauncherModel final {
  public:
    LauncherModel();

    [[nodiscard]] const std::vector<ModelField> &fields() const noexcept { return fields_; }
    [[nodiscard]] std::string value(std::string_view flag) const;
    void set(std::string_view flag, std::string value);
    void remove_pack();

    // RF-1.4: ordered takes with explicit repetitions.
    [[nodiscard]] const std::vector<std::string> &takes() const;
    void add_take(std::string path);
    void remove_take(std::size_t position);
    void move_take(std::size_t from, std::size_t to);
    void repeat_take(std::size_t position);
    void set_values(std::string_view flag, std::vector<std::string> values);

    // RF-1.6: the environment fills what the user did not select.
    void set_environment(EnvironmentValues environment);
    [[nodiscard]] std::optional<std::string> environment_issue(std::string_view flag) const;

    [[nodiscard]] audio_qa::ReplayRequest request() const;
    [[nodiscard]] std::vector<audio_qa::FieldIssue> missing_required() const;

    // RF-1.7, RF-2.2: any change requires a new preflight before starting.
    void revalidate(const Preflighter &preflight);
    [[nodiscard]] bool needs_validation() const noexcept { return dirty_; }
    [[nodiscard]] const std::vector<audio_qa::FieldIssue> &issues() const noexcept {
        return issues_;
    }
    [[nodiscard]] std::optional<std::string> issue_of(std::string_view flag) const;
    [[nodiscard]] std::optional<std::string> take_issue(std::size_t position) const;
    [[nodiscard]] std::vector<SummaryLine> summary() const;
    [[nodiscard]] std::optional<audio_qa::ValueSource> source_of(std::string_view key) const;

    [[nodiscard]] bool can_start() const;
    [[nodiscard]] bool start_enabled() const { return can_start(); }
    [[nodiscard]] bool cancel_enabled() const noexcept { return active_; }

    // RF-2.3: phases of the request in progress.
    void on_started();
    void on_phase(audio_qa::RequestPhase phase);
    void on_finished(const audio_qa::RequestOutcome &outcome);
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] audio_qa::RequestPhase phase() const noexcept { return phase_; }
    [[nodiscard]] std::optional<std::size_t> current_take() const noexcept { return current_take_; }

    // RF-2.4, RF-2.12: the results of the last request.
    [[nodiscard]] const std::vector<TakeResultRow> &results() const noexcept { return results_; }
    [[nodiscard]] std::optional<LauncherText> joint_result() const noexcept { return joint_; }
    [[nodiscard]] const std::vector<audio_qa::FieldIssue> &run_issues() const noexcept {
        return run_issues_;
    }

  private:
    [[nodiscard]] ModelField *find(std::string_view flag);
    [[nodiscard]] const ModelField *find(std::string_view flag) const;
    [[nodiscard]] bool from_environment(std::string_view key) const;

    std::vector<ModelField> fields_;
    EnvironmentValues environment_;
    std::vector<audio_qa::EffectiveValue> effective_;
    std::vector<audio_qa::FieldIssue> issues_;
    bool dirty_{true};
    bool active_{};
    audio_qa::RequestPhase phase_{};
    std::optional<std::size_t> current_take_;
    std::vector<TakeResultRow> results_;
    std::optional<LauncherText> joint_;
    std::vector<audio_qa::FieldIssue> run_issues_;
};

} // namespace ayther::replay_qa_launcher
