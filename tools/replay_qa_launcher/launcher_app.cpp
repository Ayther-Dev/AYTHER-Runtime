// Spec 002 (plan §5.14): the replay QA launcher window, SDL3 + Dear ImGui with the SDL
// renderer. It only lays out what launcher_view derives from the model, opens the SDL3 file
// dialogs and runs the request with LauncherRunner. `--smoke-frames N` draws N frames and
// exits, for the GPU smoke tests; `--smoke-capture <file.bmp>` also keeps the last frame.
// `--idle-seconds N` stays idle N seconds and reports the frames drawn and the CPU used (RNF-2).
#include "effective_values.h"
#include "environment_resolver.h"
#include "launcher_messages.h"
#include "launcher_model.h"
#include "launcher_runner.h"
#include "launcher_view.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_stdlib.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace la = ayther::replay_qa_launcher;
namespace qa = ayther::audio_qa;

namespace {

constexpr std::size_t max_report_lines = 200;
const ImVec4 error_colour{0.95F, 0.45F, 0.40F, 1.0F};

struct Options {
    int smoke_frames{-1};
    std::string smoke_capture;
    // BR-172: the interface smoke test, with the materials it selects.
    bool self_test{};
    std::string runtime;
    std::string core;
    std::string rom;
    std::string take;
    std::string output;
    // DI-23: the fields of one campaign attempt, as `--flag=value` lines.
    std::string prefill;
    // RNF-2: stays idle this many seconds and reports the frames drawn and the CPU used.
    int idle_seconds{-1};
};

Options parse_options(int argc, char **argv) {
    Options options;
    for (int index = 1; index < argc; ++index)
        if (std::string_view{argv[index]} == "--self-test")
            options.self_test = true;
    for (int index = 1; index + 1 < argc; ++index) {
        const std::string_view name{argv[index]};
        if (name == "--runtime")
            options.runtime = argv[index + 1];
        else if (name == "--core")
            options.core = argv[index + 1];
        else if (name == "--rom")
            options.rom = argv[index + 1];
        else if (name == "--take")
            options.take = argv[index + 1];
        else if (name == "--output")
            options.output = argv[index + 1];
        else if (name == "--prefill")
            options.prefill = argv[index + 1];
        if (std::string_view{argv[index]} == "--smoke-frames") {
            const std::string_view value{argv[index + 1]};
            int frames{};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), frames);
            if (parsed.ec == std::errc{} && frames > 0)
                options.smoke_frames = frames;
        } else if (std::string_view{argv[index]} == "--smoke-capture") {
            options.smoke_capture = argv[index + 1];
        } else if (std::string_view{argv[index]} == "--idle-seconds") {
            const std::string_view value{argv[index + 1]};
            int seconds{};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seconds);
            if (parsed.ec == std::errc{} && seconds > 0)
                options.idle_seconds = seconds;
        }
    }
    return options;
}

// BR-166 (RF-1.1): the SDL3 dialogs answer on their own thread; the paths are queued and the
// interface thread applies them to the model.
struct DialogResult {
    std::string flag;
    std::vector<std::string> paths;
};

struct DialogQueue {
    std::mutex mutex;
    std::vector<DialogResult> results;
};

struct DialogRequest {
    DialogQueue *queue{};
    std::string flag;
    // SDL3 reads the filters until the callback runs; the names and patterns are literals.
    std::vector<SDL_DialogFileFilter> filters;
};

void SDLCALL on_dialog(void *userdata, const char *const *files, int /*filter*/) {
    const std::unique_ptr<DialogRequest> request{static_cast<DialogRequest *>(userdata)};
    if (files == nullptr)
        return;
    DialogResult result{request->flag, {}};
    for (const char *const *file = files; *file != nullptr; ++file)
        result.paths.emplace_back(*file);
    if (result.paths.empty())
        return;
    const std::lock_guard lock{request->queue->mutex};
    request->queue->results.push_back(std::move(result));
}

void open_dialog(SDL_Window *window, DialogQueue &queue, std::string_view flag) {
    const auto spec = la::dialog_for(flag);
    if (spec.kind == la::FileDialog::none)
        return;
    auto request = std::make_unique<DialogRequest>();
    request->queue = &queue;
    request->flag = std::string{flag};
    for (const auto &filter : spec.filters)
        request->filters.push_back({filter.name.data(), filter.pattern.data()});
    auto *userdata = request.release();
    if (spec.kind == la::FileDialog::open_folder) {
        SDL_ShowOpenFolderDialog(on_dialog, userdata, window, nullptr, false);
        return;
    }
    SDL_ShowOpenFileDialog(on_dialog, userdata, window, userdata->filters.data(),
                           static_cast<int>(userdata->filters.size()), nullptr,
                           spec.kind == la::FileDialog::open_files);
}

void save_frame(SDL_Renderer *renderer, const std::string &path) {
    SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
    if (surface == nullptr || !SDL_SaveBMP(surface, path.c_str()))
        std::fprintf(stderr, "ayther_replay_qa: capture failed: %s\n", SDL_GetError());
    SDL_DestroySurface(surface);
}

// The default ImGui font lacks «…»; Segoe UI covers the texts of the launcher when present.
void load_font() {
    const char *windows = SDL_getenv("WINDIR");
    if (windows == nullptr)
        return;
    const auto font = std::filesystem::path{windows} / "Fonts" / "segoeui.ttf";
    std::error_code error;
    if (std::filesystem::is_regular_file(font, error) && !error)
        ImGui::GetIO().Fonts->AddFontFromFileTTF(font.string().c_str(), 18.0F);
}

std::string base_directory() {
    const char *base = SDL_GetBasePath();
    return base == nullptr ? std::string{} : std::string{base};
}

class LauncherApp final {
  public:
    explicit LauncherApp(SDL_Window *window) : window_(window) { resolve_environment(); }

    void frame() {
        apply_dialogs();
        drain_runner();
        if (model_.needs_validation() && !model_.active() && !ImGui::IsAnyItemActive())
            model_.revalidate(
                [](const qa::ReplayRequest &request) { return qa::preflight(request); });
        language_ = la::parse_launcher_language(model_.value("--language"));

        const auto *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("##launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        const float form_width = ImGui::GetContentRegionAvail().x * 0.55F;
        ImGui::BeginChild("##form", ImVec2{form_width, 0.0F}, ImGuiChildFlags_Borders);
        ImGui::BeginDisabled(model_.active());
        draw_sections();
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##request", ImVec2{0.0F, 0.0F}, ImGuiChildFlags_Borders);
        draw_controls();
        draw_summary();
        draw_results();
        draw_report();
        ImGui::EndChild();
        ImGui::End();
    }

    void cancel() noexcept { runner_.cancel(); }

    // BR-172: the interface smoke test drives the same model and runner as a person.
    [[nodiscard]] la::LauncherModel &model() noexcept { return model_; }
    [[nodiscard]] la::LauncherLanguage language() const noexcept { return language_; }
    [[nodiscard]] bool start() {
        if (!model_.start_enabled() || !runner_.start(model_.request()))
            return false;
        model_.on_started();
        return true;
    }

  private:
    [[nodiscard]] std::string_view text(la::LauncherText key) const noexcept {
        return la::launcher_text(language_, key);
    }

    void resolve_environment() {
        const auto play_config = qa::default_play_config_path();
        model_.set_environment(
            la::resolve_environment(base_directory(), play_config.value_or(std::filesystem::path{}),
                                    model_.value("--rom")));
    }

    void apply_dialogs() {
        std::vector<DialogResult> results;
        {
            const std::lock_guard lock{dialogs_.mutex};
            results = std::exchange(dialogs_.results, {});
        }
        for (auto &result : results) {
            if (model_.active())
                continue;
            if (result.flag == "--take") {
                for (auto &path : result.paths)
                    model_.add_take(std::move(path));
                continue;
            }
            model_.set(result.flag, std::move(result.paths.front()));
            if (result.flag == "--rom")
                resolve_environment();
        }
    }

    void drain_runner() {
        for (auto &event : runner_.drain()) {
            if (const auto *phase = std::get_if<qa::RequestPhase>(&event)) {
                model_.on_phase(*phase);
            } else if (const auto *outcome = std::get_if<qa::RequestOutcome>(&event)) {
                model_.on_finished(*outcome);
            } else if (auto *line = std::get_if<std::string>(&event)) {
                report_.push_back(std::move(*line));
                if (report_.size() > max_report_lines)
                    report_.pop_front();
            }
        }
    }

    void draw_error(std::string_view flag) const {
        if (const auto error = la::field_error(model_, language_, flag))
            ImGui::TextColored(error_colour, "%s", error->c_str());
    }

    void draw_sections() {
        for (const auto &section : la::launcher_sections()) {
            if (section.fields.empty())
                continue;
            const std::string title{la::section_title(language_, section.category)};
            if (!ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                continue;
            for (const auto &field : section.fields) {
                ImGui::PushID(field.flag.data(), field.flag.data() + field.flag.size());
                draw_field(field);
                ImGui::PopID();
            }
        }
    }

    void draw_field(const la::LauncherSectionField &field) {
        std::string label{la::field_label(language_, field.launcher_field)};
        if (field.required)
            label += " *";
        ImGui::TextUnformatted(label.c_str());
        if (field.flag == "--take") {
            draw_takes();
        } else if (field.input == la::FieldInput::choice) {
            draw_choice(field);
        } else if (field.input == la::FieldInput::key_value) {
            draw_key_values(field);
        } else {
            draw_text(field);
        }
        draw_error(field.flag);
        ImGui::Spacing();
    }

    void draw_text(const la::LauncherSectionField &field) {
        auto value = model_.value(field.flag);
        const bool dialog = la::dialog_for(field.flag).kind != la::FileDialog::none;
        const float buttons = dialog ? 120.0F : 0.0F;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttons -
                                (field.flag == "--pack" ? 120.0F : 0.0F));
        if (ImGui::InputText("##value", &value))
            model_.set(field.flag, std::move(value));
        if (dialog) {
            ImGui::SameLine();
            if (ImGui::Button(std::string{text(la::LauncherText::choose_file)}.c_str()))
                open_dialog(window_, dialogs_, field.flag);
        }
        if (field.flag == "--pack") {
            ImGui::SameLine();
            if (ImGui::Button(std::string{text(la::LauncherText::remove_pack)}.c_str()))
                model_.remove_pack();
            if (model_.value("--pack").empty())
                ImGui::TextDisabled("%s", std::string{text(la::LauncherText::no_pack)}.c_str());
        }
    }

    void draw_choice(const la::LauncherSectionField &field) {
        const auto current = model_.value(field.flag);
        ImGui::SetNextItemWidth(200.0F);
        if (!ImGui::BeginCombo("##choice", current.c_str()))
            return;
        for (const auto value : field.values) {
            const std::string item{value};
            if (ImGui::Selectable(item.c_str(), item == current))
                model_.set(field.flag, item);
        }
        ImGui::EndCombo();
    }

    // RF-1.5: repeatable `key=value` options, one per line, in order.
    void draw_key_values(const la::LauncherSectionField &field) {
        std::string joined;
        for (const auto &found : model_.fields())
            if (found.descriptor->flag == field.flag)
                for (const auto &value : found.values)
                    joined += value + '\n';
        if (!ImGui::InputTextMultiline("##values", &joined, ImVec2{-1.0F, 60.0F}))
            return;
        std::vector<std::string> values;
        std::istringstream lines{joined};
        for (std::string line; std::getline(lines, line);)
            if (!line.empty())
                values.push_back(std::move(line));
        model_.set_values(field.flag, std::move(values));
    }

    // RF-1.4: the ordered takes, with explicit repetitions.
    void draw_takes() {
        const auto takes = model_.takes();
        if (takes.empty())
            ImGui::TextDisabled("%s", std::string{text(la::LauncherText::no_takes)}.c_str());
        for (std::size_t position = 0; position < takes.size(); ++position) {
            ImGui::PushID(static_cast<int>(position));
            ImGui::Text("%zu. %s", position + 1U, takes[position].c_str());
            if (ImGui::SmallButton(std::string{text(la::LauncherText::move_up)}.c_str()) &&
                position > 0U)
                model_.move_take(position, position - 1U);
            ImGui::SameLine();
            if (ImGui::SmallButton(std::string{text(la::LauncherText::move_down)}.c_str()))
                model_.move_take(position, position + 1U);
            ImGui::SameLine();
            if (ImGui::SmallButton(std::string{text(la::LauncherText::repeat_take)}.c_str()))
                model_.repeat_take(position);
            ImGui::SameLine();
            if (ImGui::SmallButton(std::string{text(la::LauncherText::remove_take)}.c_str()))
                model_.remove_take(position);
            if (const auto issue = model_.take_issue(position))
                ImGui::TextColored(error_colour, "%s", issue->c_str());
            ImGui::PopID();
        }
        if (ImGui::Button(std::string{text(la::LauncherText::add_take)}.c_str()))
            open_dialog(window_, dialogs_, "--take");
    }

    void draw_controls() {
        ImGui::BeginDisabled(!model_.start_enabled());
        if (ImGui::Button(std::string{text(la::LauncherText::start)}.c_str()) &&
            runner_.start(model_.request()))
            model_.on_started();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!model_.cancel_enabled());
        if (ImGui::Button(std::string{text(la::LauncherText::cancel)}.c_str()))
            runner_.cancel();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextUnformatted(la::phase_line(model_, language_).c_str());
        ImGui::Separator();
    }

    // BR-168 (RF-1.6, RF-1.7): the effective values and every field issue.
    void draw_summary() {
        ImGui::SeparatorText(std::string{text(la::LauncherText::summary_title)}.c_str());
        if (ImGui::BeginTable("##summary", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            for (const auto &row : la::summary_rows(model_, language_)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.label.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", row.value.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", row.source.c_str());
            }
            ImGui::EndTable();
        }
        auto issues = model_.missing_required();
        issues.insert(issues.end(), model_.issues().begin(), model_.issues().end());
        issues.insert(issues.end(), model_.run_issues().begin(), model_.run_issues().end());
        if (issues.empty())
            return;
        ImGui::SeparatorText(std::string{text(la::LauncherText::issues_title)}.c_str());
        for (const auto &issue : issues)
            ImGui::TextColored(error_colour, "%s: %s", issue.field.c_str(), issue.code.c_str());
    }

    // BR-170 (RF-2.4, RF-2.12): per take, apart, and the joint result.
    void draw_results() {
        const auto view = la::results_view(model_, language_);
        if (view.rows.empty() && view.joint.empty())
            return;
        ImGui::SeparatorText(std::string{text(la::LauncherText::results_title)}.c_str());
        if (ImGui::BeginTable("##results", 5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("#");
            ImGui::TableSetupColumn("");
            ImGui::TableSetupColumn(std::string{text(la::LauncherText::playback)}.c_str());
            ImGui::TableSetupColumn(std::string{text(la::LauncherText::traversal)}.c_str());
            ImGui::TableSetupColumn(std::string{text(la::LauncherText::evidence)}.c_str());
            ImGui::TableHeadersRow();
            for (const auto &row : view.rows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.position.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", row.take.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.playback.c_str());
                if (!row.diagnostic.empty())
                    ImGui::TextDisabled("%s", row.diagnostic.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.traversal.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.evidence.c_str());
                if (!row.reasons.empty())
                    ImGui::TextDisabled("%s", row.reasons.c_str());
            }
            ImGui::EndTable();
        }
        if (!view.joint.empty()) {
            ImGui::TextUnformatted(view.joint.c_str());
            ImGui::TextDisabled("%s", view.note.c_str());
        }
    }

    void draw_report() {
        if (report_.empty())
            return;
        ImGui::Separator();
        ImGui::BeginChild("##report", ImVec2{0.0F, 0.0F});
        for (const auto &line : report_)
            ImGui::TextUnformatted(line.c_str());
        ImGui::EndChild();
    }

    SDL_Window *window_;
    la::LauncherModel model_;
    la::LauncherRunner runner_;
    DialogQueue dialogs_;
    la::LauncherLanguage language_{la::LauncherLanguage::spanish};
    std::deque<std::string> report_;
};

// Spec 002, BR-172 (RF-1.1, RF-2.1): the steps of the interface smoke test, one per frame.
class SelfTest final {
  public:
    explicit SelfTest(const Options &options) : options_(options) {}

    // False once the test ended; `passed` tells how.
    [[nodiscard]] bool step(LauncherApp &app) {
        auto &model = app.model();
        const auto language = app.language();
        const auto elapsed = std::chrono::steady_clock::now() - started_;
        if (elapsed > std::chrono::seconds{75})
            return fail("timed out in step " + std::to_string(static_cast<int>(stage_)));
        switch (stage_) {
        case Stage::required: {
            // RF-1.1, RF-1.2: without materials the ROM and the take are required.
            const auto required =
                std::string{la::launcher_text(language, la::LauncherText::required_missing)};
            if (la::field_error(model, language, "--rom") != required ||
                la::field_error(model, language, "--take") != required || model.start_enabled())
                return fail("the required fields are not flagged");
            std::printf("ayther_replay_qa: self_test required_errors=shown\n");
            model.set("--runtime", options_.runtime);
            model.set("--core", options_.core);
            model.set("--rom", options_.rom);
            model.set("--output", options_.output);
            model.add_take(options_.take);
            stage_ = Stage::valid;
            return true;
        }
        case Stage::valid:
            if (model.needs_validation())
                return true;
            if (!model.issues().empty())
                return fail("the request is not valid: " + model.issues().front().field + " " +
                            model.issues().front().code);
            if (!app.start())
                return fail("Start did not start the request");
            std::printf("ayther_replay_qa: self_test started\n");
            stage_ = Stage::running;
            return true;
        case Stage::running:
            if (model.phase().kind == qa::RequestPhaseKind::running) {
                running_since_ = std::chrono::steady_clock::now();
                stage_ = Stage::cancel;
            }
            return true;
        case Stage::cancel:
            // Some replay first, then Cancel as a person would press it.
            if (std::chrono::steady_clock::now() - running_since_ < std::chrono::seconds{2})
                return true;
            if (!model.cancel_enabled())
                return fail("Cancel is not enabled while running");
            app.cancel();
            std::printf("ayther_replay_qa: self_test cancel_requested\n");
            stage_ = Stage::result;
            return true;
        case Stage::result: {
            // The phase closes before the outcome arrives from the worker thread.
            if (model.active() || !model.joint_result())
                return true;
            const auto view = la::results_view(model, language);
            if (view.rows.size() != 1U || view.joint.empty())
                return fail("no result was shown");
            std::printf("ayther_replay_qa: self_test result playback=\"%s\" joint=\"%s\"\n",
                        view.rows.front().playback.c_str(), view.joint.c_str());
            if (view.rows.front().playback !=
                la::launcher_text(language, la::LauncherText::playback_cancelled))
                return fail("the take was not cancelled");
            passed_ = true;
            return false;
        }
        }
        return false;
    }
    [[nodiscard]] bool passed() const noexcept { return passed_; }

  private:
    enum class Stage { required, valid, running, cancel, result };
    [[nodiscard]] bool fail(const std::string &reason) {
        std::fprintf(stderr, "ayther_replay_qa: self_test failed: %s\n", reason.c_str());
        return false;
    }

    const Options &options_;
    Stage stage_{Stage::required};
    std::chrono::steady_clock::time_point started_{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point running_since_;
    bool passed_{};
};

// The CPU time used by this process, user and kernel, in milliseconds.
std::uint64_t process_cpu_ms() noexcept {
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return 0U;
    const auto ticks = [](const FILETIME &time) {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32U) | time.dwLowDateTime;
    };
    return (ticks(kernel) + ticks(user)) / 10'000U;
#else
    return static_cast<std::uint64_t>(std::clock()) * 1000U / CLOCKS_PER_SEC;
#endif
}

} // namespace

int main(int argc, char **argv) {
    const auto options = parse_options(argc, argv);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "ayther_replay_qa: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    const std::string title{
        la::launcher_text(la::LauncherLanguage::spanish, la::LauncherText::window_title)};
    if (!SDL_CreateWindowAndRenderer(title.c_str(), 1280, 820, SDL_WINDOW_RESIZABLE, &window,
                                     &renderer)) {
        std::fprintf(stderr, "ayther_replay_qa: window failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    load_font();
    // RNF-2: at most one redraw per display refresh.
    (void)SDL_SetRenderVSync(renderer, 1);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    int frames = 0;
    std::uint64_t idle_frames = 0U;
    std::optional<bool> self_test_passed;
    {
        LauncherApp app{window};
        // DI-23: a rejected prefill leaves the form empty; the person fills it as usual.
        if (!options.prefill.empty()) {
            std::ifstream input{std::filesystem::path{options.prefill}, std::ios::binary};
            const std::string text{std::istreambuf_iterator<char>{input},
                                   std::istreambuf_iterator<char>{}};
            const auto rejected = input ? la::apply_prefill(app.model(), text)
                                        : std::vector<std::string>{options.prefill};
            for (const auto &line : rejected)
                std::fprintf(stderr, "ayther_replay_qa: prefill_rejected: %s\n", line.c_str());
            if (rejected.empty())
                std::printf("ayther_replay_qa: prefill_applied\n");
        }
        std::optional<SelfTest> self_test;
        if (options.self_test)
            self_test.emplace(options);
        const int wait_ms = la::idle_wait_ms(options.smoke_frames > 0 || options.self_test);
        const auto idle_started = std::chrono::steady_clock::now();
        const auto idle_cpu_started = process_cpu_ms();
        bool running = true;
        while (running) {
            SDL_Event event;
            // RNF-2: waits for input (or the next refresh of the progress) instead of spinning.
            bool pending =
                wait_ms > 0 ? SDL_WaitEventTimeout(&event, wait_ms) : SDL_PollEvent(&event);
            while (pending) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT)
                    running = false;
                pending = SDL_PollEvent(&event);
            }
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            app.frame();
            if (self_test && !self_test->step(app))
                running = false;
            ImGui::Render();
            SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
            const bool last = options.smoke_frames > 0 && ++frames >= options.smoke_frames;
            if (last && !options.smoke_capture.empty())
                save_frame(renderer, options.smoke_capture);
            SDL_RenderPresent(renderer);
            ++idle_frames;
            if (last)
                running = false;
            if (options.idle_seconds > 0 && std::chrono::steady_clock::now() - idle_started >=
                                                std::chrono::seconds{options.idle_seconds}) {
                std::printf("ayther_replay_qa: idle frames=%llu cpu_ms=%llu\n",
                            static_cast<unsigned long long>(idle_frames),
                            static_cast<unsigned long long>(process_cpu_ms() - idle_cpu_started));
                running = false;
            }
        }
        // Closing the window cancels the request in progress; the runner joins its thread.
        app.cancel();
        if (self_test)
            self_test_passed = self_test->passed();
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (options.smoke_frames > 0)
        std::printf("ayther_replay_qa: smoke frames=%d\n", frames);
    if (self_test_passed) {
        std::printf("ayther_replay_qa: self_test %s\n", *self_test_passed ? "passed" : "failed");
        return *self_test_passed ? 0 : 1;
    }
    return 0;
}
