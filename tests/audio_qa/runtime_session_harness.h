#pragma once

// Spec 002 (BR-137 to BR-155): a synthetic supervisor for the replay inspection tests. It
// launches the real Runtime over the inherited channels, sends one request, optionally a
// scripted input (AYTHER_QA_INPUT_SCRIPT) and a cancel, and keeps everything the Runtime
// sends: live states, inspection and render facts, the engine facts per frame, the PCM and
// every terminal, also those of a post-end inspection.
#include "cancellation_message.h"
#include "control_message.h"
#include "fact_batch.h"
#include "inherited_channel.h"
#include "inspection_facts.h"
#include "pcm_message.h"
#include "protocol_header.h"
#include "protocol_io.h"
#include "replay_execution_result.h"
#include "runtime_process.h"
#include "runtime_protocol_v11.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace ayther::audio_qa::harness {

namespace fs = std::filesystem;

enum class CancelWhen { never, paused, playing, interrupted };

// Prints every live state as it arrives (AYTHER_QA_HARNESS_VERBOSE).
inline const bool verbose = [] {
    char *raw{};
    std::size_t size{};
    const bool set = _dupenv_s(&raw, &size, "AYTHER_QA_HARNESS_VERBOSE") == 0 && raw != nullptr;
    std::free(raw);
    return set;
}();

struct Options {
    fs::path runtime;
    fs::path core;
    fs::path rom;
    fs::path take;
    fs::path root;
    std::string label;
    std::optional<std::string> language{"es"};
    std::string script;
    std::string presentation{"none"};
    std::size_t take_position{};
    bool last_take{};
    CancelWhen cancel_when{CancelWhen::never};
    std::vector<RuntimeEnvironmentEntry> extra_environment;
    // Extra Runtime arguments: pack, trust registry, conditions (BR-156 with Toma 3).
    std::vector<std::wstring> extra_arguments;
};

struct TimedState {
    ReplayStateView state;
    double at_ms{};
};

struct Session {
    std::vector<TimedState> states;
    std::vector<InspectionEvent> events;
    std::vector<RenderFrameFact> render_frames;
    std::vector<RunOpened> runs_opened;
    std::vector<ReplayExecutionResult> terminals;
    // Engine facts (not the Runtime's own) by run and emulation frame.
    std::map<std::pair<std::string, std::uint64_t>, std::uint64_t> engine_facts_by_frame;
    std::uint64_t engine_facts{};
    std::uint64_t duplicate_fact_ids{};
    std::uint64_t pcm_bytes{};
    std::uint64_t pcm_chunks{};
    std::size_t statuses{};
    bool stream_valid{true};
    double cancel_to_terminal_ms{};
    double terminal_at_ms{};
    std::int32_t exit_code{-1};
};

inline std::wstring wide(const std::string &text) { return fs::path{text}.wstring(); }

inline std::optional<std::wstring> environment_value(const wchar_t *name) {
    wchar_t *raw{};
    std::size_t size{};
    if (_wdupenv_s(&raw, &size, name) != 0 || raw == nullptr)
        return std::nullopt;
    std::wstring value{raw};
    std::free(raw);
    return value;
}

inline Session run(const Options &options) {
    Session session;
    const auto root = options.root / options.label;
    fs::remove_all(root);
    fs::create_directories(root / "appdata");
    std::vector<RuntimeEnvironmentEntry> environment{{L"APPDATA", (root / "appdata").wstring()}};
    if (options.presentation == "visible") {
        for (const auto *key : {L"SystemRoot", L"WINDIR", L"TEMP", L"TMP"})
            if (const auto value = environment_value(key))
                environment.emplace_back(key, *value);
    } else {
        environment.insert(environment.end(), {{L"SDL_AUDIO_DRIVER", L"dummy"},
                                               {L"SDL_AUDIO_FREQUENCY", L"44100"},
                                               {L"SDL_AUDIO_CHANNELS", L"2"},
                                               {L"SDL_AUDIO_DEVICE_SAMPLE_FRAMES", L"64"}});
    }
    environment.insert(environment.end(), options.extra_environment.begin(),
                       options.extra_environment.end());
    if (!options.script.empty()) {
        std::ofstream{root / "input.script", std::ios::binary} << options.script;
        environment.emplace_back(L"AYTHER_QA_INPUT_SCRIPT", (root / "input.script").wstring());
    }
    auto control_created = create_inherited_control_channel();
    auto data_created = create_inherited_data_channel();
    auto *control = std::get_if<InheritedControlChannel>(&control_created);
    auto *data = std::get_if<InheritedDataChannel>(&data_created);
    const auto identity = identify_runtime_binary(options.runtime);
    const auto *runtime = std::get_if<RuntimeBinaryIdentity>(&identity);
    if (control == nullptr || data == nullptr || runtime == nullptr) {
        session.stream_valid = false;
        return session;
    }
    const std::string run_id = "run-" + options.label;
    std::vector<std::wstring> arguments{
        L"--qa-session",
        L"--qa-control-channel",
        wide(inherited_data_channel_token(control->runtime_read.get())),
        L"--qa-data-channel",
        wide(inherited_data_channel_token(data->runtime_write.get())),
        L"--qa-run-id",
        wide(run_id),
        L"--core",
        options.core.wstring(),
        L"--rom",
        options.rom.wstring(),
        L"--qa-presentation",
        wide(options.presentation),
        L"--qa-take-position",
        std::to_wstring(options.take_position)};
    if (options.last_take)
        arguments.emplace_back(L"--qa-last-take");
    arguments.insert(arguments.end(), options.extra_arguments.begin(),
                     options.extra_arguments.end());
    const std::array<NativeChannelHandle, 2> inherited{control->runtime_read.get(),
                                                       data->runtime_write.get()};
    auto launched = launch_runtime_process(*runtime, arguments, environment, inherited);
    auto *process = std::get_if<OwnedRuntimeProcess>(&launched);
    if (process == nullptr) {
        session.stream_valid = false;
        return session;
    }
    control->runtime_read.reset();
    data->runtime_write.reset();
    const auto started = std::chrono::steady_clock::now();
    const auto now_ms = [&started] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    };

    Request request{
        "request-" + options.label,      "session-" + options.label, "conditions-" + options.label,
        {options.take.generic_string()}, Admission::pending,         options.language};
    const auto encoded = encode_request_message(request, 1U);
    const auto *bytes = std::get_if<std::vector<std::byte>>(&encoded);
    if (bytes == nullptr || !write_channel(control->supervisor_write, *bytes)) {
        session.stream_valid = false;
        return session;
    }
    const auto admission = read_protocol_message(data->supervisor_read, 2U);
    const auto *admission_bytes = std::get_if<std::vector<std::byte>>(&admission);
    if (admission_bytes == nullptr ||
        !std::holds_alternative<Request>(decode_admission_message(*admission_bytes, 1U))) {
        session.stream_valid = false;
        return session;
    }

    bool cancel_sent{};
    double cancel_at{};
    const auto send_cancel = [&] {
        const auto message = encode_cancellation_message(
            {request.request_id, run_id, CancellationStage::requested, 0U, std::nullopt}, 2U);
        const auto *cancel = std::get_if<std::vector<std::byte>>(&message);
        cancel_sent = cancel != nullptr && write_channel(control->supervisor_write, *cancel);
        cancel_at = now_ms();
    };
    std::set<std::tuple<std::string, std::string, std::uint64_t>> fact_ids;
    std::uint64_t sequence = 2U;
    for (;;) {
        const auto received = read_protocol_message(data->supervisor_read, 2U);
        const auto *message = std::get_if<std::vector<std::byte>>(&received);
        // After the last terminal the channel closes with the Runtime.
        if (message == nullptr || message->size() < protocol_header_bytes) {
            if (session.terminals.empty())
                session.stream_valid = false;
            break;
        }
        const auto header =
            decode_protocol_header(std::span{*message}.first(protocol_header_bytes), 2U);
        if (header.error != HeaderError::none || header.header.channel_sequence != sequence) {
            session.stream_valid = false;
            break;
        }
        if (header.header.type == MessageType::session_status) {
            ++session.statuses;
            const auto status = decode_session_status(*message, sequence);
            if (const auto *state = std::get_if<ReplayStateView>(&status)) {
                if (verbose)
                    std::fprintf(stderr, "[%s] %.0f ms %s %s\n", options.label.c_str(), now_ms(),
                                 state->phase.c_str(),
                                 state->frame ? std::to_string(*state->frame).c_str() : "-");
                session.states.push_back({*state, now_ms()});
                if (!cancel_sent &&
                    ((options.cancel_when == CancelWhen::paused && state->phase == "paused") ||
                     (options.cancel_when == CancelWhen::playing && state->phase == "playing") ||
                     (options.cancel_when == CancelWhen::interrupted &&
                      state->phase == "interrupted")))
                    send_cancel();
            } else if (const auto *opened = std::get_if<RunOpened>(&status)) {
                session.runs_opened.push_back(*opened);
            } else {
                session.stream_valid = false;
            }
        } else if (header.header.type == MessageType::fact_batch) {
            const auto decoded = decode_fact_batch(*message, sequence);
            const auto *facts = std::get_if<std::vector<Fact>>(&decoded);
            if (facts == nullptr) {
                session.stream_valid = false;
                break;
            }
            for (const auto &fact : *facts) {
                if (!fact_ids
                         .insert({fact.id.run_id, fact.id.producer_id, fact.id.producer_sequence})
                         .second)
                    ++session.duplicate_fact_ids;
                if (auto event = read_inspection_event(fact)) {
                    session.events.push_back(*event);
                } else if (auto frame = read_render_frame(fact)) {
                    session.render_frames.push_back(*frame);
                } else if (fact.id.producer_id.starts_with("engine-")) {
                    ++session.engine_facts;
                    if (fact.frame_index.value)
                        ++session.engine_facts_by_frame[{fact.id.run_id, *fact.frame_index.value}];
                }
            }
        } else if (header.header.type == MessageType::audio_chunk) {
            const auto decoded = decode_pcm_message(*message, sequence);
            if (const auto *chunk = std::get_if<AudioChunk>(&decoded)) {
                session.pcm_bytes += chunk->bytes.size();
                ++session.pcm_chunks;
            } else {
                session.stream_valid = false;
            }
        } else if (header.header.type == MessageType::terminal) {
            const auto decoded = decode_replay_execution_result(*message, sequence);
            if (const auto *result = std::get_if<ReplayExecutionResult>(&decoded))
                session.terminals.push_back(*result);
            else
                session.stream_valid = false;
            session.terminal_at_ms = now_ms();
            if (cancel_sent && session.cancel_to_terminal_ms == 0.0)
                session.cancel_to_terminal_ms = now_ms() - cancel_at;
        } else {
            session.stream_valid = false;
        }
        ++sequence;
    }
    if (!process->wait(15000U, session.exit_code))
        session.stream_valid = false;
    return session;
}

} // namespace ayther::audio_qa::harness
