// Spec 002 (plan §5.14): the pure launcher model. One group per task:
//   BR-159 (RF-1.1, RF-1.2, RF-1.3) fields from the descriptors, required fields, pack
//          removable, no material at start.
//   BR-160 (RF-1.6) effective values with their origin and «Sin pack».
//   BR-161 (RF-1.4) ordered takes with explicit repetitions.
//   BR-162 (RF-1.7, RF-2.2) a change revalidates with the preflight, flags the
//          incompatibility and blocks Start without changing another selection.
//   BR-163 (RF-2.3, RF-2.5) phases and buttons while a request is active.
//   BR-164 (RF-2.4, RF-2.12) results per take, apart, and the joint result.
#include "check_option_descriptors.h"
#include "launcher_model.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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

void fields_and_required() {
    la::LauncherModel model;
    const auto descriptors = qa::check_option_descriptors();
    expect(model.fields().size() == descriptors.size(),
           "RF-1.5: one field per option of the table");
    expect(model.value("--rom").empty() && model.takes().empty() && model.value("--pack").empty(),
           "RF-1.2, RF-1.3: the launcher starts without materials of an earlier session");
    expect(model.value("--presentation") == "visible" && model.value("--language") == "es",
           "plan §5.14: visible presentation and Spanish at start");
    expect(!model.can_start(), "RF-1.2: without ROM and take Start is disabled");
    const auto missing = model.missing_required();
    const auto flagged = [&missing](std::string_view field) {
        return std::any_of(missing.begin(), missing.end(),
                           [field](const qa::FieldIssue &issue) { return issue.field == field; });
    };
    expect(flagged("--rom") && flagged("--take"), "RF-1.2: the missing ROM and take are flagged");
    auto with_rom = ready();
    expect(with_rom.missing_required().empty(), "RF-1.1: ROM, take, runtime and output suffice");
    with_rom.set("--pack", "C:/packs/game.ay");
    with_rom.remove_pack();
    expect(with_rom.value("--pack").empty() && with_rom.request().pack.empty(),
           "RF-1.3: the pack can be removed");
}

qa::PreflightResult fake_preflight(const qa::ReplayRequest &request) {
    qa::PreflightResult result;
    result.effective = {
        {"rom", request.rom, qa::ValueSource::explicit_option},
        {"core", request.core, qa::ValueSource::explicit_option},
        {"pack", request.pack.empty() ? "none" : request.pack, qa::ValueSource::explicit_option},
        {"language", "es", qa::ValueSource::default_value}};
    if (request.rom.ends_with(".sfc"))
        result.issues.push_back({"--core", "core_platform_mismatch"});
    if (request.takes.size() > 1U)
        result.issues.push_back({"--take[1]", "take_game_mismatch"});
    return result;
}

void effective_values() {
    auto model = ready();
    model.revalidate(fake_preflight);
    const auto summary = model.summary();
    const auto line = [&summary](std::string_view key) {
        return std::find_if(summary.begin(), summary.end(),
                            [key](const la::SummaryLine &item) { return item.key == key; });
    };
    expect(line("pack") != summary.end() && line("pack")->no_pack,
           "RF-1.3: without pack the summary says «Sin pack»");
    expect(line("language") != summary.end() &&
               line("language")->source == qa::ValueSource::default_value,
           "RF-1.6: each value keeps its origin");
    model.set_environment({std::string{"C:/install/ayther_runtime.exe"}, std::nullopt});
    model.set("--runtime", "");
    model.revalidate(fake_preflight);
    expect(model.request().runtime == "C:/install/ayther_runtime.exe" &&
               model.source_of("runtime") == qa::ValueSource::environment,
           "RF-1.6: a value resolved by the environment shows that origin");
}

void ordered_takes() {
    la::LauncherModel model;
    model.add_take("A");
    model.add_take("B");
    model.repeat_take(0);
    expect(model.takes() == std::vector<std::string>{"A", "B", "A"},
           "RF-1.4: repeating a take adds it explicitly at the end");
    model.move_take(2, 1);
    expect(model.takes() == std::vector<std::string>{"A", "A", "B"},
           "RF-1.4: moving keeps the rest");
    model.remove_take(0);
    expect(model.takes() == std::vector<std::string>{"A", "B"} &&
               model.request().takes == std::vector<std::string>{"A", "B"},
           "RF-1.4: the request keeps the exact order and no implicit take");
    model.move_take(5, 0);
    model.remove_take(9);
    expect(model.takes() == std::vector<std::string>{"A", "B"},
           "RF-1.4: an invalid position changes nothing");
}

void incompatibilities() {
    auto model = ready();
    model.revalidate(fake_preflight);
    expect(model.can_start() && model.issues().empty(), "a valid request can start");
    model.set("--rom", "C:/roms/game.sfc");
    expect(model.needs_validation() && !model.can_start(),
           "RF-1.7: a change requires a new validation before starting");
    model.revalidate(fake_preflight);
    expect(model.issue_of("--core") == "core_platform_mismatch" && !model.can_start(),
           "RF-1.7: the incompatibility is flagged in its field and blocks Start");
    expect(model.value("--core") == "C:/cores/core.dll" && model.takes().size() == 1U,
           "RF-1.7: no other selection changes silently");
    model.set("--rom", "C:/roms/game.md");
    model.add_take("C:/takes/other.ayr");
    model.revalidate(fake_preflight);
    expect(model.issue_of("--take") == "take_game_mismatch" &&
               model.take_issue(1) == "take_game_mismatch" && !model.take_issue(0),
           "RF-2.2: the issue of a take names its position");
    la::LauncherModel real;
    real.set("--runtime", "C:/missing/runtime.exe");
    real.set("--rom", "C:/missing/game.md");
    real.set("--core", "C:/missing/core.dll");
    real.set("--output", "C:/missing/evidence");
    real.add_take("C:/missing/take.ayr");
    real.revalidate([](const qa::ReplayRequest &request) { return qa::preflight(request); });
    expect(real.issue_of("--rom") == "material_not_found" && !real.can_start(),
           "RF-2.2: the real preflight flags a missing material in its field");
}

void phases_and_buttons() {
    auto model = ready();
    model.revalidate(fake_preflight);
    expect(model.start_enabled() && !model.cancel_enabled(), "Start enabled when ready");
    model.on_started();
    expect(model.active() && !model.start_enabled() && model.cancel_enabled(),
           "RF-2.3: while active, Start is disabled and Cancel enabled");
    model.on_phase({qa::RequestPhaseKind::running, 0});
    expect(model.phase().kind == qa::RequestPhaseKind::running && model.current_take() == 0U,
           "RF-2.3: the phase and the take in progress are shown");
    model.on_phase({qa::RequestPhaseKind::closed, 0});
    expect(!model.active() && model.start_enabled() && !model.cancel_enabled(),
           "RF-2.5: once closed the request can be started again");
}

void results() {
    auto model = ready();
    qa::RequestOutcome outcome;
    qa::TakeOutcome first;
    first.position = 0;
    first.take = "a.ayr";
    first.playback = {qa::PlaybackKind::natural_end, {}};
    first.traversal = qa::TraversalKind::linear;
    first.evidence = {true, {}};
    first.linear_completed = true;
    qa::TakeOutcome second = first;
    second.position = 1;
    second.playback = {qa::PlaybackKind::cancelled, {}};
    second.evidence = {false, {"data_lost"}};
    second.linear_completed = false;
    outcome.per_take = {first, second,
                        qa::NotStartedTake{2, "c.ayr", "not_started_after_cancellation"}};
    outcome.exit_code = 2;
    outcome.linear_complete = false;
    model.on_finished(outcome);
    const auto &rows = model.results();
    expect(rows.size() == 3U && rows[0].playback == la::LauncherText::playback_natural_end &&
               rows[0].traversal == la::LauncherText::traversal_linear &&
               rows[0].evidence == la::LauncherText::evidence_complete,
           "RF-2.4: each take shows playback, traversal and evidence apart");
    expect(rows[1].playback == la::LauncherText::playback_cancelled &&
               rows[1].evidence == la::LauncherText::evidence_incomplete &&
               rows[1].reasons == std::vector<std::string>{"data_lost"},
           "RF-2.4: a cancelled take keeps its evidence reasons");
    expect(rows[2].playback == la::LauncherText::playback_not_started,
           "RF-2.12: a take that did not start is named");
    expect(model.joint_result() == la::LauncherText::joint_not_linear_complete,
           "RF-2.12: the joint result follows BR-065");
    outcome.per_take = {first};
    outcome.linear_complete = true;
    model.on_finished(outcome);
    expect(model.joint_result() == la::LauncherText::joint_linear_complete,
           "RF-2.12: only every take linear, natural and complete credits the request");
}

} // namespace

// D-10 (campaign 2026-10-04): Windows "Copy as path" pastes the path between double quotes. A
// path field keeps the path without that one surrounding pair, so the effective value shown and
// the one recorded are the path itself (RF-1.6). Other fields keep what was typed.
void pasted_paths_lose_their_quotes() {
    const auto quoted = [](std::string_view path) { return "\"" + std::string{path} + "\""; };
    auto model = ready();
    std::size_t paths{};
    for (const auto &descriptor : qa::check_option_descriptors()) {
        if (descriptor.kind != qa::CheckOptionKind::path || descriptor.repeatable)
            continue;
        ++paths;
        const std::string path = "C:/Users/qa/My Files/" + std::string{descriptor.flag.substr(2)};
        model.set(descriptor.flag, quoted(path));
        expect(model.value(descriptor.flag) == path,
               "D-10: the pasted path of " + std::string{descriptor.flag} + " loses its quotes");
    }
    expect(paths >= 6U, "D-10: every path field is covered");
    model.add_take(quoted("C:/takes/Toma 3.ayr"));
    model.set_values("--take", {quoted("C:/takes/b.ayr"), quoted("C:/takes/Toma 3.ayr")});
    expect(model.takes() == std::vector<std::string>{"C:/takes/b.ayr", "C:/takes/Toma 3.ayr"},
           "D-10: a pasted take path loses its quotes");
    const auto request = model.request();
    expect(request.trust_registry == "C:/Users/qa/My Files/trust-registry" &&
               request.pack == "C:/Users/qa/My Files/pack" &&
               request.rom == "C:/Users/qa/My Files/rom",
           "D-10, RF-1.6: the request records the unquoted paths");
    model.revalidate(fake_preflight);
    const auto summary = model.summary();
    const auto pack = std::find_if(summary.begin(), summary.end(),
                                   [](const la::SummaryLine &line) { return line.key == "pack"; });
    expect(pack != summary.end() && pack->value == "C:/Users/qa/My Files/pack",
           "D-10, RF-1.6: the effective value shown is the unquoted path");

    model.set("--rom", "\"C:/roms/half.md");
    expect(model.value("--rom") == "\"C:/roms/half.md",
           "D-10: a single quote is not a surrounding pair and stays");
    model.set("--rom", "\"\"C:/roms/twice.md\"\"");
    expect(model.value("--rom") == "\"C:/roms/twice.md\"", "D-10: only one pair is removed");
    model.set("--request-id", quoted("id"));
    expect(model.value("--request-id") == quoted("id"), "D-10: a text field keeps its quotes");
}

// DI-23 (RF-1.1, RF-1.6, RF-1.8): the acceptance campaign opens the launcher with the fields
// of one attempt already written. Every value is explicit, nothing else is selected, and an
// unknown or malformed line rejects the whole prefill instead of applying part of it.
void prefill() {
    la::LauncherModel model;
    const auto errors = la::apply_prefill(model, "--runtime=C:/qa/ayther_runtime.exe\n"
                                                 "--rom=C:/roms/Golden Axe.md\n"
                                                 "--core=C:/cores/core.dll\n"
                                                 "--take=C:/takes/Toma 3.ayr\n"
                                                 "--take=C:/takes/Toma 3.ayr\n"
                                                 "--pack=\"C:/packs/Golden Axe.ay\"\n"
                                                 "--profile=full\n"
                                                 "--shaders=on\n"
                                                 "--core-option=no_sprite_limit=enabled\n"
                                                 "--output=C:/evidence/attempt\n"
                                                 "--request-id=campaign-attempt-1\n");
    expect(errors.empty(), "DI-23: a valid prefill applies without errors");
    expect(model.value("--rom") == "C:/roms/Golden Axe.md" &&
               model.value("--pack") == "C:/packs/Golden Axe.ay" &&
               model.value("--profile") == "full" && model.value("--shaders") == "on" &&
               model.value("--request-id") == "campaign-attempt-1",
           "DI-23: every prefilled field holds its value, pasted quotes removed");
    expect(model.takes().size() == 2U && model.takes()[1] == "C:/takes/Toma 3.ayr",
           "DI-23: repeated takes keep their order and repetition (RF-1.4)");
    model.revalidate(fake_preflight);
    const auto source = model.source_of("rom");
    expect(source && *source == qa::ValueSource::explicit_option,
           "DI-23, RF-1.6: a prefilled value is explicit, never inferred");
    expect(model.value("--video-output").empty() && model.value("--patch").empty(),
           "DI-23, RF-1.8: a field absent from the prefill stays unselected");

    la::LauncherModel rejected;
    const auto bad = la::apply_prefill(rejected, "--rom=C:/roms/game.md\n--unknown=1\nnoise\n");
    expect(bad.size() == 2U && rejected.value("--rom").empty(),
           "DI-23: unknown or malformed lines reject the whole prefill");
}

int main() {
    prefill();
    fields_and_required();
    pasted_paths_lose_their_quotes();
    effective_values();
    ordered_takes();
    incompatibilities();
    phases_and_buttons();
    results();
    if (failures != 0)
        return 1;
    std::cout << "the launcher model follows plan §5.14\n";
    return 0;
}
