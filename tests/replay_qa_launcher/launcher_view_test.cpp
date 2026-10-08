// Spec 002 (plan §5.14): what the window of the launcher draws, without GPU. One group per task:
//   BR-166 (RF-1.1) ROM, takes and pack are chosen with the SDL3 file dialogs; the initial
//          presentation is visible.
//   BR-167 (RF-1.5) the sections show each option of the table with the validation of its kind.
//   BR-168 (RF-1.6, RF-1.7) the effective summary with «Sin pack» and the errors per field.
//   BR-170 (RF-2.4, RF-2.12) the results per take and the joint result, localized.
#include "check_option_descriptors.h"
#include "launcher_view.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>

namespace la = ayther::replay_qa_launcher;
namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

la::LauncherModel ready() {
    la::LauncherModel model;
    model.set("--runtime", "C:/qa/ayther_runtime.exe");
    model.set("--rom", "C:/roms/game.md");
    model.set("--core", "C:/cores/core.dll");
    model.set("--output", "C:/evidence");
    model.add_take("C:/takes/a.ayr");
    return model;
}

bool has_pattern(const la::DialogSpec &spec, std::string_view pattern) {
    return std::any_of(spec.filters.begin(), spec.filters.end(),
                       [pattern](const la::DialogFilter &filter) {
                           return filter.pattern.find(pattern) != std::string_view::npos;
                       });
}

void dialogs() {
    const auto rom = la::dialog_for("--rom");
    expect(rom.kind == la::FileDialog::open_file && has_pattern(rom, "md"),
           "RF-1.1: the ROM is chosen with a file dialog");
    const auto takes = la::dialog_for("--take");
    expect(takes.kind == la::FileDialog::open_files && has_pattern(takes, "ayr"),
           "RF-1.1, RF-1.4: several takes can be chosen at once");
    const auto pack = la::dialog_for("--pack");
    expect(pack.kind == la::FileDialog::open_file && has_pattern(pack, "ay"),
           "RF-1.1, RF-1.3: the pack is chosen with a file dialog");
    expect(la::dialog_for("--output").kind == la::FileDialog::open_folder,
           "the destination is a folder");
    expect(la::dialog_for("--language").kind == la::FileDialog::none,
           "a choice has no file dialog");
    expect(la::LauncherModel{}.value("--presentation") == "visible",
           "RF-1.1: the initial presentation is visible");
}

void sections_and_validation() {
    const auto sections = la::launcher_sections();
    expect(sections.size() == 8U && sections.front().category == qa::CheckOptionCategory::rom,
           "RF-1.5: one section per category, materials first");
    expect(la::input_for(qa::CheckOptionKind::choice) == la::FieldInput::choice &&
               la::input_for(qa::CheckOptionKind::path) == la::FieldInput::path &&
               la::input_for(qa::CheckOptionKind::unsigned_integer) == la::FieldInput::number &&
               la::input_for(qa::CheckOptionKind::key_value) == la::FieldInput::key_value,
           "RF-1.5: each kind has its input");
    const auto *language = qa::find_check_option("--language");
    const auto *subsystems = qa::find_check_option("--subsystems");
    const auto *core_option = qa::find_check_option("--core-option");
    expect(la::local_issue(*language, "fr") == "unsupported_language" &&
               !la::local_issue(*language, "en"),
           "RF-1.5: a choice accepts only its values");
    expect(la::local_issue(*subsystems, "12x") == "invalid_unsigned_value" &&
               la::local_issue(*subsystems, "99999999999") == "invalid_unsigned_value" &&
               !la::local_issue(*subsystems, "7") && !la::local_issue(*subsystems, ""),
           "RF-1.5: a number accepts only an unsigned 32-bit value");
    expect(la::local_issue(*core_option, "=x") == "malformed_core_option" &&
               la::local_issue(*core_option, "novalue") == "malformed_core_option" &&
               !la::local_issue(*core_option, "region=eu"),
           "RF-1.5: a core option is key=value");
    auto model = ready();
    model.set("--language", "fr");
    expect(la::field_error(model, la::LauncherLanguage::spanish, "--language") ==
               "unsupported_language",
           "RF-1.5: the field shows its validation");
    model.set_values("--core-option", {"region=eu", "broken"});
    expect(la::field_error(model, la::LauncherLanguage::spanish, "--core-option") ==
               "malformed_core_option",
           "RF-1.5: each value of a repeatable field is validated");
}

qa::PreflightResult incompatible(const qa::ReplayRequest &request) {
    qa::PreflightResult result;
    result.effective = {{"rom", request.rom, qa::ValueSource::explicit_option},
                        {"pack", "none", qa::ValueSource::explicit_option},
                        {"language", "es", qa::ValueSource::default_value}};
    result.issues.push_back({"--core", "core_platform_mismatch"});
    return result;
}

void summary_and_errors() {
    auto model = ready();
    model.revalidate(incompatible);
    const auto rows = la::summary_rows(model, la::LauncherLanguage::spanish);
    const auto pack = std::find_if(rows.begin(), rows.end(),
                                   [](const la::SummaryRow &row) { return row.key == "pack"; });
    expect(pack != rows.end() && pack->value == "Sin pack" && pack->label == "Pack",
           "RF-1.3, RF-1.6: the summary shows «Sin pack»");
    const auto language = std::find_if(
        rows.begin(), rows.end(), [](const la::SummaryRow &row) { return row.key == "language"; });
    expect(language != rows.end() &&
               language->source ==
                   la::source_text(la::LauncherLanguage::spanish, qa::ValueSource::default_value),
           "RF-1.6: each row names its origin");
    expect(la::field_error(model, la::LauncherLanguage::spanish, "--core") ==
               "core_platform_mismatch",
           "RF-1.7: the incompatibility is shown in its field");
    la::LauncherModel empty;
    expect(la::field_error(empty, la::LauncherLanguage::english, "--rom") ==
               la::launcher_text(la::LauncherLanguage::english, la::LauncherText::required_missing),
           "RF-1.2: a missing required field says so");
    expect(!la::field_error(model, la::LauncherLanguage::spanish, "--rom"),
           "a valid field shows no error");
    empty.revalidate([](const qa::ReplayRequest &) {
        qa::PreflightResult result;
        result.effective = {{"core", "C:/cores/default.dll", qa::ValueSource::environment}};
        result.issues.push_back({"--rom", "material_not_found"});
        return result;
    });
    expect(la::field_error(empty, la::LauncherLanguage::spanish, "--rom") ==
               la::launcher_text(la::LauncherLanguage::spanish, la::LauncherText::required_missing),
           "RF-1.2: an empty required field says it is missing before any preflight issue");
    expect(!la::field_error(empty, la::LauncherLanguage::spanish, "--core"),
           "RF-1.6: a core the preflight resolved from the environment is not flagged");
}

void results() {
    auto model = ready();
    qa::RequestOutcome outcome;
    qa::TakeOutcome first;
    first.position = 0;
    first.take = "a.ayr";
    first.playback = {qa::PlaybackKind::cancelled, {}};
    first.traversal = qa::TraversalKind::linear;
    first.evidence = {false, {"data_lost", "not_confirmed"}};
    outcome.per_take = {first};
    outcome.linear_complete = false;
    model.on_finished(outcome);
    const auto view = la::results_view(model, la::LauncherLanguage::english);
    expect(view.rows.size() == 1U && view.rows[0].position == "1" &&
               view.rows[0].playback == la::launcher_text(la::LauncherLanguage::english,
                                                          la::LauncherText::playback_cancelled) &&
               view.rows[0].traversal == la::launcher_text(la::LauncherLanguage::english,
                                                           la::LauncherText::traversal_linear) &&
               view.rows[0].reasons == "data_lost, not_confirmed",
           "RF-2.4: each take shows playback, traversal and evidence apart, localized");
    expect(view.joint == la::launcher_text(la::LauncherLanguage::english,
                                           la::LauncherText::joint_not_linear_complete) &&
               view.note == la::launcher_text(la::LauncherLanguage::english,
                                              la::LauncherText::not_a_visual_evaluation),
           "RF-2.12: the joint result and its scope are localized");
    expect(la::results_view(ready(), la::LauncherLanguage::spanish).joint.empty(),
           "without a finished request there is no joint result");
    model.on_started();
    model.on_phase({qa::RequestPhaseKind::running, 1});
    expect(la::phase_line(model, la::LauncherLanguage::spanish).find('2') != std::string::npos,
           "RF-2.3: the phase names the take in progress, from 1");
}

// Campaign 2026-10-07 (RNF-2, D-6b): the launcher stays open while the Runtime presents the
// replay; redrawing without waiting took a whole core and delayed frames of the replay.
void pacing() {
    expect(la::idle_wait_ms(false) >= 50 && la::idle_wait_ms(false) <= 250,
           "RNF-2: the window waits for input, redrawing at least a few times per second");
    expect(la::idle_wait_ms(true) == 0, "BR-157/BR-172: the smoke tests draw without waiting");
}

} // namespace

int main() {
    pacing();
    dialogs();
    sections_and_validation();
    summary_and_errors();
    results();
    if (failures != 0)
        return 1;
    std::cout << "the launcher window draws the model\n";
    return 0;
}
