// Spec 002, BR-135 (RF-2.3, RF-2.5; contracts.md C1, C2): a synthetic supervisor drives the
// real Runtime with the synthetic core, without GPU.
//   - A 1.1 request (it carries its language) receives `replay_state` with the run, the take
//     position and the phase; a scripted pause sends an `inspection_event` fact; while paused
//     the Runtime keeps reading the control channel and a `cancel` ends the take as cancelled
//     within the 2 s of P-6.
//   - A `cancel` while playing ends the take as cancelled as well.
//   - A 1.0 request (no language) receives no `session_status` after its admission (RNF-6).
// Arguments: runtime, core, ROM, take, long take, work directory.
#include "cancellation_message.h"
#include "control_message.h"
#include "fact_batch.h"
#include "inherited_channel.h"
#include "protocol_header.h"
#include "protocol_io.h"
#include "replay_execution_result.h"
#include "runtime_process.h"
#include "runtime_protocol_v11.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

struct Paths {
    fs::path runtime;
    fs::path core;
    fs::path rom;
    fs::path take;
    fs::path long_take;
    fs::path work;
};

struct Session {
    std::vector<qa::ReplayStateView> states;
    std::vector<qa::InspectionEvent> events;
    std::optional<qa::ReplayExecutionResult> terminal;
    std::size_t statuses{};
    double cancel_to_terminal_ms{};
    std::int32_t exit_code{-1};
};

enum class CancelWhen { never, paused, playing };

Session run(const Paths &paths, const std::string &label, const fs::path &take,
            const std::optional<std::string> &language, const std::string &script,
            CancelWhen cancel_when) {
    Session session;
    const auto root = paths.work / label;
    fs::remove_all(root);
    fs::create_directories(root / "appdata");
    std::vector<qa::RuntimeEnvironmentEntry> environment{
        {L"APPDATA", (root / "appdata").wstring()},
        {L"SDL_AUDIO_DRIVER", L"dummy"},
        {L"SDL_AUDIO_FREQUENCY", L"44100"},
        {L"SDL_AUDIO_CHANNELS", L"2"},
        {L"SDL_AUDIO_DEVICE_SAMPLE_FRAMES", L"64"}};
    if (!script.empty()) {
        std::ofstream{root / "input.script", std::ios::binary} << script;
        environment.emplace_back(L"AYTHER_QA_INPUT_SCRIPT", (root / "input.script").wstring());
    }
    auto control_created = qa::create_inherited_control_channel();
    auto data_created = qa::create_inherited_data_channel();
    auto *control = std::get_if<qa::InheritedControlChannel>(&control_created);
    auto *data = std::get_if<qa::InheritedDataChannel>(&data_created);
    const auto identity = qa::identify_runtime_binary(paths.runtime);
    const auto *runtime = std::get_if<qa::RuntimeBinaryIdentity>(&identity);
    if (control == nullptr || data == nullptr || runtime == nullptr) {
        expect(false, label + ": channels and runtime are available");
        return session;
    }
    const std::string run_id = "run-v11-" + label;
    const std::vector<std::wstring> arguments{
        L"--qa-session",
        L"--qa-control-channel",
        std::filesystem::path{qa::inherited_data_channel_token(control->runtime_read.get())}
            .wstring(),
        L"--qa-data-channel",
        std::filesystem::path{qa::inherited_data_channel_token(data->runtime_write.get())}
            .wstring(),
        L"--qa-run-id",
        std::filesystem::path{run_id}.wstring(),
        L"--core",
        paths.core.wstring(),
        L"--rom",
        paths.rom.wstring(),
        L"--qa-presentation",
        L"none",
        L"--qa-take-position",
        L"2"};
    const std::array<qa::NativeChannelHandle, 2> inherited{control->runtime_read.get(),
                                                           data->runtime_write.get()};
    auto launched = qa::launch_runtime_process(*runtime, arguments, environment, inherited);
    auto *process = std::get_if<qa::OwnedRuntimeProcess>(&launched);
    if (process == nullptr) {
        expect(false, label + ": the Runtime starts");
        return session;
    }
    control->runtime_read.reset();
    data->runtime_write.reset();

    qa::Request request{"request-" + label,      "session-" + label,     "conditions-" + label,
                        {take.generic_string()}, qa::Admission::pending, language};
    const auto encoded = qa::encode_request_message(request, 1U);
    const auto *bytes = std::get_if<std::vector<std::byte>>(&encoded);
    expect(bytes != nullptr && qa::write_channel(control->supervisor_write, *bytes),
           label + ": the request is delivered");
    const auto admission = qa::read_protocol_message(data->supervisor_read, 2U);
    const auto *admission_bytes = std::get_if<std::vector<std::byte>>(&admission);
    expect(admission_bytes != nullptr && std::holds_alternative<qa::Request>(
                                             qa::decode_admission_message(*admission_bytes, 1U)),
           label + ": the request is admitted");

    bool cancel_sent{};
    std::chrono::steady_clock::time_point cancel_at;
    const auto send_cancel = [&] {
        const auto message = qa::encode_cancellation_message(
            {request.request_id, run_id, qa::CancellationStage::requested, 0U, std::nullopt}, 2U);
        const auto *cancel = std::get_if<std::vector<std::byte>>(&message);
        cancel_sent = cancel != nullptr && qa::write_channel(control->supervisor_write, *cancel);
        cancel_at = std::chrono::steady_clock::now();
    };
    std::uint64_t sequence = 2U;
    for (;;) {
        const auto received = qa::read_protocol_message(data->supervisor_read, 2U);
        const auto *message = std::get_if<std::vector<std::byte>>(&received);
        if (message == nullptr || message->size() < qa::protocol_header_bytes) {
            expect(false, label + ": the data channel stays readable until the terminal");
            break;
        }
        const auto header =
            qa::decode_protocol_header(std::span{*message}.first(qa::protocol_header_bytes), 2U);
        expect(header.error == qa::HeaderError::none && header.header.channel_sequence == sequence,
               label + ": the data channel keeps its sequence");
        if (header.header.type == qa::MessageType::session_status) {
            ++session.statuses;
            const auto status = qa::decode_session_status(*message, sequence);
            if (const auto *state = std::get_if<qa::ReplayStateView>(&status)) {
                session.states.push_back(*state);
                if (!cancel_sent && cancel_when == CancelWhen::paused && state->phase == "paused")
                    send_cancel();
                if (!cancel_sent && cancel_when == CancelWhen::playing && state->phase == "playing")
                    send_cancel();
            } else {
                expect(false, label + ": every session_status is a valid replay state");
            }
        } else if (header.header.type == qa::MessageType::fact_batch) {
            const auto decoded = qa::decode_fact_batch(*message, sequence);
            if (const auto *facts = std::get_if<std::vector<qa::Fact>>(&decoded))
                for (const auto &fact : *facts)
                    if (auto event = qa::read_inspection_event(fact))
                        session.events.push_back(*event);
        } else if (header.header.type == qa::MessageType::terminal) {
            const auto decoded = qa::decode_replay_execution_result(*message, sequence);
            if (const auto *result = std::get_if<qa::ReplayExecutionResult>(&decoded))
                session.terminal = *result;
            if (cancel_sent)
                session.cancel_to_terminal_ms = std::chrono::duration<double, std::milli>(
                                                    std::chrono::steady_clock::now() - cancel_at)
                                                    .count();
            break;
        }
        ++sequence;
    }
    if (!process->wait(10000U, session.exit_code))
        expect(false, label + ": the Runtime exits after its terminal");
    return session;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 7) {
        std::cerr << "usage: runtime_session_v11_test <runtime> <core> <rom> <take> <long take> "
                     "<work>\n";
        return 2;
    }
    const Paths paths{argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]};

    // C1-3, C2, C1-1: pause by script, then cancel while paused.
    const auto paused = run(paths, "paused", paths.long_take, std::string{"es"},
                            "frame=2 key space down\nafter=0 key space up\n", CancelWhen::paused);
    expect(!paused.states.empty() && paused.states.front().phase == "preparing" &&
               !paused.states.front().frame,
           "RF-2.3: the first state is preparing, without a frame");
    bool positions = !paused.states.empty();
    for (const auto &state : paused.states)
        positions = positions && state.run_id == "run-v11-paused" && state.take_position == 2U &&
                    state.frames_total == 600U;
    expect(positions, "RF-2.3: every state names its run, its take position and N");
    const auto paused_state =
        std::find_if(paused.states.begin(), paused.states.end(),
                     [](const qa::ReplayStateView &state) { return state.phase == "paused"; });
    // The key arrives in the wait after frame 2, with no frame in progress: frame 2 stays and
    // frame 3 is never started (spec.md, Pausa; RF-4.1).
    expect(paused_state != paused.states.end() && paused_state->frame == 2U,
           "RF-4.1: the pause requested after frame 2 keeps frame 2");
    expect(paused.events.size() == 1U && paused.events[0].control == "pause" &&
               paused.events[0].seq == 1U && paused.events[0].frame_before == 2U &&
               paused.events[0].frame_after == 2U && paused.events[0].visit == 1U &&
               paused.events[0].elapsed_ms == 50U,
           "C2: the pause is an inspection_event at frame 2, first visit, 50 ms of take");
    expect(paused.terminal && paused.terminal->playback == "cancelled" &&
               paused.terminal->traversal == "linear" && paused.terminal->inputs_consumed == 3U,
           "RF-2.5, RF-4.1: a cancel while paused ends the take at frame 2; the input of frame 3 "
           "was not consumed");
    expect(paused.cancel_to_terminal_ms > 0.0 && paused.cancel_to_terminal_ms < 2000.0,
           "P-6: the paused Runtime attends the cancel within 2 s");
    expect(!paused.states.empty() && paused.states.back().phase == "closing",
           "C1-3: the last state is closing");

    // C1-1: a cancel while playing.
    const auto playing =
        run(paths, "playing", paths.long_take, std::string{"en"}, {}, CancelWhen::playing);
    expect(playing.terminal && playing.terminal->playback == "cancelled" &&
               playing.terminal->inputs_consumed < 600U && playing.cancel_to_terminal_ms < 2000.0,
           "RF-2.5: a cancel while playing ends the take before its end within 2 s");

    // RNF-6: a 1.0 request is served without the messages of 1.1.
    const auto legacy = run(paths, "legacy", paths.take, std::nullopt,
                            "frame=2 key space down\nafter=0 key space up\npaused=2 key space "
                            "down\nafter=0 key space up\n",
                            CancelWhen::never);
    expect(legacy.statuses == 0U && legacy.events.empty(),
           "RNF-6: protocol 1.0 receives no session_status and no inspection_event");
    expect(legacy.terminal && legacy.terminal->playback == "natural_end" &&
               legacy.terminal->inputs_consumed == 6U,
           "RNF-6: the 1.0 take still ends naturally");

    std::cout << "paused states=" << paused.states.size() << " events=" << paused.events.size()
              << " cancel_ms=" << paused.cancel_to_terminal_ms
              << " playing_cancel_ms=" << playing.cancel_to_terminal_ms << '\n';
    if (failures != 0)
        return 1;
    std::cout << "the Runtime speaks protocol 1.1 to a synthetic supervisor\n";
    return 0;
}
