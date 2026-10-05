#include "check_execution.h"

#include "cancellation_message.h"
#include "capability_gate.h"
#include "capability_report.h"
#include "control_message.h"
#include "durable_file.h"
#include "fact_batch.h"
#include "incremental_evidence.h"
#include "inherited_channel.h"
#include "inspection_evidence.h"
#include "isolated_runtime_data.h"
#include "pcm_message.h"
#include "play_launch_manifest.h"
#include "protocol_header.h"
#include "protocol_io.h"
#include "reference_store.h"
#include "replay_execution_result.h"
#include "runtime_process.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace ayther::audio_qa {
namespace {

[[nodiscard]] std::wstring wide(const std::string &value) {
    return std::filesystem::path{value}.wstring();
}

[[nodiscard]] std::optional<std::filesystem::path> environment_path(const char *const name) {
#ifdef _WIN32
    char *raw{};
    std::size_t size{};
    if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr || size <= 1U) {
        std::free(raw);
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> value{raw, &std::free};
    return std::filesystem::path{value.get()};
#else
    const auto *const value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
        return std::nullopt;
    return std::filesystem::path{value};
#endif
}

struct RuntimeDataLocation {
    std::filesystem::path user_root;
    std::wstring environment_key;
};

[[nodiscard]] RuntimeDataLocation runtime_data_location() {
#ifdef _WIN32
    auto base = environment_path("APPDATA");
    if (!base) {
        if (const auto profile = environment_path("USERPROFILE"))
            base = *profile / "AppData" / "Roaming";
    }
    return {(base ? *base : std::filesystem::current_path()) / "Ayther", L"APPDATA"};
#elif defined(__APPLE__)
    const auto home = environment_path("HOME");
    return {(home ? *home / "Library" / "Application Support" : std::filesystem::current_path()) /
                "Ayther",
            L"HOME"};
#else
    auto base = environment_path("XDG_DATA_HOME");
    if (!base) {
        if (const auto home = environment_path("HOME"))
            base = *home / ".local" / "share";
    }
    return {(base ? *base : std::filesystem::current_path()) / "Ayther", L"XDG_DATA_HOME"};
#endif
}

// Watches the cancel token while a take runs and sends `cancel` (contracts.md C1) over
// the control channel once. The channel stays open until stop(), after the terminal.
class CancellationForwarder final {
  public:
    CancellationForwarder(OwnedChannelHandle &channel, const CancelToken *token,
                          std::string request_id, std::string run_id)
        : channel_(channel), token_(token), request_id_(std::move(request_id)),
          run_id_(std::move(run_id)) {
        if (token_ != nullptr)
            watcher_ = std::thread{[this] { watch(); }};
    }
    ~CancellationForwarder() { stop(); }
    CancellationForwarder(const CancellationForwarder &) = delete;
    CancellationForwarder &operator=(const CancellationForwarder &) = delete;

    void stop() noexcept {
        stopping_.store(true, std::memory_order_release);
        if (watcher_.joinable())
            watcher_.join();
        channel_.reset();
    }

  private:
    void watch() noexcept {
        while (!stopping_.load(std::memory_order_acquire)) {
            if (token_->requested()) {
                const auto encoded = encode_cancellation_message(
                    {request_id_, run_id_, CancellationStage::requested, 0U, std::nullopt}, 2U);
                if (const auto *bytes = std::get_if<std::vector<std::byte>>(&encoded))
                    (void)write_channel(channel_, *bytes);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
    }

    OwnedChannelHandle &channel_;
    const CancelToken *token_;
    std::string request_id_;
    std::string run_id_;
    std::atomic<bool> stopping_{};
    std::thread watcher_;
};

void append_value_argument(std::vector<std::wstring> &arguments, const std::wstring_view option,
                           const std::optional<std::string> &value) {
    if (!value || value->empty())
        return;
    arguments.emplace_back(option);
    arguments.push_back(wide(*value));
}

} // namespace

std::vector<std::wstring>
runtime_replay_arguments(const EffectiveRequest &request, const std::string_view control_token,
                         const std::string_view data_token, const std::string_view run_id,
                         const std::size_t take_position, const bool last_take) {
    std::vector<std::wstring> arguments{L"--qa-session",
                                        L"--qa-control-channel",
                                        wide(std::string{control_token}),
                                        L"--qa-data-channel",
                                        wide(std::string{data_token}),
                                        L"--qa-run-id",
                                        wide(std::string{run_id}),
                                        L"--core",
                                        wide(request.core),
                                        L"--rom",
                                        wide(request.rom)};
    if (request.pack && request.pack_mode == "hd") {
        arguments.emplace_back(L"--pack");
        arguments.push_back(wide(*request.pack));
    }
    append_value_argument(arguments, L"--manifest", request.play_manifest);
    append_value_argument(arguments, L"--qa-presentation", request.presentation);
    const auto &conditions = request.conditions;
    // D-1: a profile only means something to a loaded pack; without one it is not sent.
    if (request.pack && request.pack_mode == "hd")
        append_value_argument(arguments, L"--profile", conditions.profile);
    append_value_argument(arguments, L"--subsystems", conditions.subsystems);
    append_value_argument(arguments, L"--mute-buses", conditions.mute_buses);
    append_value_argument(arguments, L"--output", conditions.video_output);
    append_value_argument(arguments, L"--patch", conditions.patch);
    if (conditions.shaders)
        arguments.emplace_back(*conditions.shaders ? L"--shaders" : L"--no-shaders");
    for (const auto &option : conditions.core_options) {
        arguments.emplace_back(L"--core-option");
        arguments.push_back(wide(option));
    }
    if (!request.trust_registry.empty()) {
        arguments.emplace_back(L"--trust-registry");
        arguments.push_back(wide(request.trust_registry));
    }
    arguments.emplace_back(L"--qa-take-position");
    arguments.push_back(std::to_wstring(take_position));
    if (last_take)
        arguments.emplace_back(L"--qa-last-take");
    return arguments;
}

CheckExecutionResult execute_check_replay(const EffectiveRequest &effective, const Request &request,
                                          std::string run_id, const ReplayExecutionControl &control,
                                          ReplayCessation &cessation) noexcept {
    cessation = {};
    try {
        if (request.take_ids.size() != 1U || run_id.empty())
            return CheckExecutionError::result_mismatch;

        const auto identified = identify_runtime_binary(effective.runtime);
        const auto *runtime = std::get_if<RuntimeBinaryIdentity>(&identified);
        if (runtime == nullptr)
            return CheckExecutionError::runtime_identity_unavailable;

        const auto data_location = runtime_data_location();
        auto prepared_data = prepare_isolated_runtime_data(data_location.user_root,
                                                           std::filesystem::path{effective.output} /
                                                               (".runtime-data-" + run_id));
        auto *prepared = std::get_if<IsolatedRuntimeData>(&prepared_data);
        if (prepared == nullptr)
            return CheckExecutionError::runtime_data_isolation_failed;
        auto isolated_data = std::move(*prepared);
        std::vector<RuntimeEnvironmentEntry> environment{
            {data_location.environment_key, isolated_data.directory().wstring()}};
        if (effective.presentation == "visible") {
            for (const auto key :
                 {"SystemRoot", "WINDIR", "TEMP", "TMP", "SDL_VIDEO_DRIVER", "SDL_AUDIO_DRIVER",
                  "SDL_AUDIO_FREQUENCY", "SDL_AUDIO_CHANNELS", "SDL_AUDIO_DEVICE_SAMPLE_FRAMES"})
                if (const auto value = environment_path(key))
                    environment.emplace_back(wide(key), value->wstring());
        }
        // BR-153: the scripted input of the QA tests reaches the Runtime in both modes.
        if (const auto script = environment_path("AYTHER_QA_INPUT_SCRIPT"))
            environment.emplace_back(L"AYTHER_QA_INPUT_SCRIPT", script->wstring());
        const auto capability_query = query_runtime_process(*runtime, {L"--qa-capabilities"},
                                                            environment, 64U * 1024U, 2000U);
        const auto *capability_output = std::get_if<RuntimeQueryOutput>(&capability_query);
        if (capability_output == nullptr || capability_output->exit_code != 0)
            return CheckExecutionError::runtime_incompatible;
        const auto decoded_capabilities =
            decode_runtime_capability_report(capability_output->output);
        const auto *offer = std::get_if<CapabilitySet>(&decoded_capabilities);
        CapabilityGate capability_gate;
        if (offer == nullptr || !capability_gate.negotiate(*offer))
            return CheckExecutionError::runtime_incompatible;
        // Contracts.md C1: visible replay negotiates 1.1; without presentation 1.1 is chosen
        // when offered and a 1.0 Runtime keeps working (RNF-6).
        const auto protocol = negotiate_runtime_protocol(*offer, effective.presentation);
        const auto *negotiated = std::get_if<ContractVersion>(&protocol);
        if (negotiated == nullptr)
            return CheckExecutionError::runtime_incompatible;
        // C1-2: a 1.1 request carries its language; the ledger keeps the 1.0 request.
        auto delivered_request = request;
        if (*negotiated == runtime_protocol_v11)
            delivered_request.language = effective.language;

        auto control_created = create_inherited_control_channel();
        auto data_created = create_inherited_data_channel();
        auto *channels = std::get_if<InheritedControlChannel>(&control_created);
        auto *data = std::get_if<InheritedDataChannel>(&data_created);
        if (channels == nullptr || data == nullptr)
            return CheckExecutionError::channel_unavailable;

        const auto encoded = encode_request_message(delivered_request, 1U);
        const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
        if (message == nullptr)
            return CheckExecutionError::request_encoding_failed;

        const auto arguments = runtime_replay_arguments(
            effective, inherited_data_channel_token(channels->runtime_read.get()),
            inherited_data_channel_token(data->runtime_write.get()), run_id, control.take_position,
            control.last_take);
        const std::array<NativeChannelHandle, 2> inherited{channels->runtime_read.get(),
                                                           data->runtime_write.get()};
        if (effective.presentation == "none")
            environment.insert(environment.end(), {{L"SDL_AUDIO_DRIVER", L"dummy"},
                                                   {L"SDL_AUDIO_FREQUENCY", L"44100"},
                                                   {L"SDL_AUDIO_CHANNELS", L"2"},
                                                   {L"SDL_AUDIO_DEVICE_SAMPLE_FRAMES", L"64"}});
        auto launched =
            launch_runtime_process(*runtime, arguments, std::move(environment), inherited);
        auto *process = std::get_if<OwnedRuntimeProcess>(&launched);
        if (process == nullptr)
            return CheckExecutionError::runtime_launch_failed;
        cessation.launched = true;
        channels->runtime_read.reset();
        data->runtime_write.reset();

        if (!write_channel(channels->supervisor_write, *message))
            return CheckExecutionError::request_delivery_failed;
        // Spec 002 (contracts.md C1): the control channel stays open until the terminal; a
        // cancellation is forwarded over it as soon as it is requested.
        CancellationForwarder forwarder{channels->supervisor_write, control.cancel,
                                        request.request_id, run_id};

        const auto admission_message = read_protocol_message(data->supervisor_read, 2U);
        const auto *admission_bytes = std::get_if<std::vector<std::byte>>(&admission_message);
        if (admission_bytes == nullptr)
            return CheckExecutionError::admission_failed;
        const auto admitted = decode_admission_message(*admission_bytes, 1U);
        const auto *accepted = std::get_if<Request>(&admitted);
        if (accepted == nullptr || accepted->admission != Admission::accepted ||
            accepted->request_id != request.request_id)
            return CheckExecutionError::admission_failed;
        if (control.on_running)
            control.on_running();

        auto opened_evidence = open_incremental_evidence(effective.output, run_id);
        auto *evidence_writer = std::get_if<IncrementalEvidenceWriter>(&opened_evidence);
        if (evidence_writer == nullptr)
            return CheckExecutionError::evidence_stream_invalid;

        CheckExecutionEvidence evidence;
        std::vector<InspectionEvent> inspection_events;
        std::uint64_t sequence = 2U;
        // RF-2.8: after the confirmed result of the last take the Runtime stays paused at N−1
        // with its window; the channel stays open until the window closes.
        bool ended_paused{};
        // RF-2.9: a post-end inspection is a run of its own, never part of the confirmed one.
        std::optional<IncrementalEvidenceWriter> post_writer;
        std::string post_run;
        std::vector<InspectionEvent> post_events;
        for (;;) {
            const auto received = read_protocol_message(data->supervisor_read, 2U);
            const auto *bytes = std::get_if<std::vector<std::byte>>(&received);
            if (ended_paused && (bytes == nullptr || bytes->size() < protocol_header_bytes))
                break;
            if (bytes == nullptr || bytes->size() < protocol_header_bytes)
                return CheckExecutionError::evidence_stream_invalid;
            const auto header =
                decode_protocol_header(std::span{*bytes}.first(protocol_header_bytes), 2U);
            if (header.error != HeaderError::none || header.header.channel_sequence != sequence)
                return CheckExecutionError::evidence_stream_invalid;
            if (ended_paused && header.header.type != MessageType::session_status &&
                header.header.type != MessageType::fact_batch &&
                header.header.type != MessageType::terminal)
                return CheckExecutionError::evidence_stream_invalid;
            if (ended_paused && header.header.type == MessageType::fact_batch) {
                const auto decoded = decode_fact_batch(*bytes, sequence);
                const auto *facts = std::get_if<std::vector<Fact>>(&decoded);
                if (facts == nullptr || !post_writer)
                    return CheckExecutionError::evidence_stream_invalid;
                for (const auto &fact : *facts)
                    if (auto event = read_inspection_event(fact))
                        post_events.push_back(std::move(*event));
                (void)post_writer->append_facts(*facts);
            } else if (ended_paused && header.header.type == MessageType::terminal) {
                const auto decoded = decode_replay_execution_result(*bytes, sequence);
                const auto *closed = std::get_if<ReplayExecutionResult>(&decoded);
                if (closed == nullptr || !post_writer || closed->run_id != post_run ||
                    closed->traversal != std::optional<std::string>{"post_end_inspection"})
                    return CheckExecutionError::evidence_stream_invalid;
                const auto directory = std::filesystem::path{effective.output} / "runs" / post_run;
                const auto payload = std::span{*bytes}.subspan(protocol_header_bytes);
                auto traversal = traversal_of_take(closed->recording_frames,
                                                   closed->inputs_consumed, post_events);
                traversal.kind = TraversalKind::post_end_inspection;
                traversal.linear_completed = false;
                if (!std::holds_alternative<DurablePublishedFile>(
                        publish_durable_file(directory / "replay-result.toml", payload)) ||
                    !std::holds_alternative<DurablePublishedFile>(
                        write_traversal(directory / "traversal.toml", traversal)))
                    return CheckExecutionError::evidence_stream_invalid;
                (void)post_writer->finish(closed->trace, false);
                post_writer.reset();
            } else if (header.header.type == MessageType::fact_batch) {
                const auto decoded = decode_fact_batch(*bytes, sequence);
                const auto *facts = std::get_if<std::vector<Fact>>(&decoded);
                if (facts == nullptr)
                    return CheckExecutionError::evidence_stream_invalid;
                // C2: inspection events build the traversal of the take (RF-5.8).
                for (const auto &fact : *facts)
                    if (auto event = read_inspection_event(fact))
                        inspection_events.push_back(std::move(*event));
                if (!evidence.preservation_error) {
                    if (const auto error = evidence_writer->append_facts(*facts))
                        evidence.preservation_error = *error;
                }
            } else if (header.header.type == MessageType::audio_chunk) {
                const auto decoded = decode_pcm_message(*bytes, sequence);
                const auto *chunk = std::get_if<AudioChunk>(&decoded);
                if (chunk == nullptr)
                    return CheckExecutionError::evidence_stream_invalid;
                if (!evidence.preservation_error) {
                    if (const auto error = evidence_writer->append_pcm(*chunk))
                        evidence.preservation_error = *error;
                }
            } else if (header.header.type == MessageType::session_status) {
                // Protocol 1.1 (contracts.md C1-3, C1-6): live state and post-end runs.
                const auto status = decode_session_status(*bytes, sequence);
                if (std::holds_alternative<ProtocolV11Error>(status))
                    return CheckExecutionError::evidence_stream_invalid;
                if (const auto *opened = std::get_if<RunOpened>(&status)) {
                    // C1-6: only after the confirmed result of the last take.
                    if (!ended_paused || post_writer)
                        return CheckExecutionError::evidence_stream_invalid;
                    auto opened_post = open_incremental_evidence(effective.output, opened->run_id);
                    auto *writer = std::get_if<IncrementalEvidenceWriter>(&opened_post);
                    if (writer == nullptr)
                        return CheckExecutionError::evidence_stream_invalid;
                    post_writer.emplace(std::move(*writer));
                    post_run = opened->run_id;
                    post_events.clear();
                }
                if (const auto *state = std::get_if<ReplayStateView>(&status);
                    state != nullptr && control.on_replay_state)
                    control.on_replay_state(*state);
            } else if (header.header.type == MessageType::terminal) {
                const auto decoded = decode_replay_execution_result(*bytes, sequence);
                const auto *result = std::get_if<ReplayExecutionResult>(&decoded);
                if (result == nullptr)
                    return CheckExecutionError::terminal_failed;
                evidence.replay = *result;
                const auto payload = std::span{*bytes}.subspan(protocol_header_bytes);
                const auto published =
                    publish_durable_file(std::filesystem::path{effective.output} / "runs" / run_id /
                                             "replay-result.toml",
                                         payload);
                if (!std::holds_alternative<DurablePublishedFile>(published))
                    return CheckExecutionError::evidence_stream_invalid;
                // Spec 002 (RF-5.8, RF-2.13): the traversal is confirmed with the result.
                if (!std::holds_alternative<DurablePublishedFile>(write_traversal(
                        std::filesystem::path{effective.output} / "runs" / run_id /
                            "traversal.toml",
                        traversal_of_take(result->recording_frames, result->inputs_consumed,
                                          inspection_events))))
                    return CheckExecutionError::evidence_stream_invalid;
                if (!result->ended_paused || effective.presentation != "visible")
                    break;
                ended_paused = true;
            } else {
                return CheckExecutionError::evidence_stream_invalid;
            }
            ++sequence;
        }
        auto &result = evidence.replay;

        if (!evidence.preservation_error) {
            const auto publication =
                evidence_writer->finish(result.trace, result.assignment_count > 0U);
            if (const auto *preserved = std::get_if<IntegratedEvidenceSummary>(&publication)) {
                evidence.preserved = *preserved;
                result.trace = evidence_writer->trace();
            } else {
                evidence.preservation_error = std::get<IntegratedEvidenceError>(publication);
            }
        }
        // RNF-5: the stream ended with the terminal and the writer concluded, complete or
        // not; nothing more is written for this take.
        cessation.evidence_closed = true;

        std::int32_t exit_code{};
        if (!process->wait(30000U, exit_code))
            return CheckExecutionError::runtime_timeout;
        cessation.exited = true;
        forwarder.stop();
        if (result.run_id != run_id || result.take_id != request.take_ids[0])
            return CheckExecutionError::result_identity_mismatch;
        if (result.succeeded && result.inputs_consumed != result.recording_frames)
            return CheckExecutionError::result_input_mismatch;
        if (result.succeeded && evidence.preserved &&
            ((result.assignment_count > 0U && !result.trace.causally_connected) ||
             !result.trace.loss_free ||
             evidence.preserved->facts != result.trace.observed_fact_count ||
             evidence.preserved->pcm_blocks == 0U))
            return CheckExecutionError::result_evidence_mismatch;
        if ((exit_code == 0) != result.succeeded)
            return CheckExecutionError::result_exit_mismatch;
        const auto unchanged = isolated_data.verify_user_data_unchanged();
        const auto *verified = std::get_if<bool>(&unchanged);
        if (verified == nullptr || !*verified)
            return CheckExecutionError::user_data_changed;
        evidence.runtime_data_isolated = true;
        return evidence;
    } catch (...) {
        return CheckExecutionError::runtime_failed;
    }
}

std::string_view check_execution_error_code(const CheckExecutionError error) noexcept {
    switch (error) {
    case CheckExecutionError::reference_unavailable:
        return "reference_unavailable";
    case CheckExecutionError::manifest_unavailable:
        return "play_manifest_unavailable";
    case CheckExecutionError::manifest_missing_material:
        return "play_manifest_missing_material";
    case CheckExecutionError::runtime_identity_unavailable:
        return "runtime_identity_unavailable";
    case CheckExecutionError::runtime_incompatible:
        return "runtime_incompatible";
    case CheckExecutionError::runtime_data_isolation_failed:
        return "runtime_data_isolation_failed";
    case CheckExecutionError::user_data_changed:
        return "user_data_changed";
    case CheckExecutionError::channel_unavailable:
        return "qa_channel_unavailable";
    case CheckExecutionError::request_encoding_failed:
        return "request_encoding_failed";
    case CheckExecutionError::runtime_launch_failed:
        return "runtime_launch_failed";
    case CheckExecutionError::request_delivery_failed:
        return "request_delivery_failed";
    case CheckExecutionError::admission_failed:
        return "runtime_admission_failed";
    case CheckExecutionError::terminal_failed:
        return "runtime_terminal_failed";
    case CheckExecutionError::runtime_timeout:
        return "runtime_timeout";
    case CheckExecutionError::runtime_failed:
        return "runtime_replay_failed";
    case CheckExecutionError::result_mismatch:
        return "runtime_replay_result_mismatch";
    case CheckExecutionError::result_identity_mismatch:
        return "runtime_replay_identity_mismatch";
    case CheckExecutionError::result_input_mismatch:
        return "runtime_replay_input_mismatch";
    case CheckExecutionError::result_evidence_mismatch:
        return "runtime_replay_evidence_mismatch";
    case CheckExecutionError::result_exit_mismatch:
        return "runtime_replay_exit_mismatch";
    case CheckExecutionError::evidence_stream_invalid:
        return "runtime_evidence_stream_invalid";
    }
    return "check_execution_failed";
}

} // namespace ayther::audio_qa
