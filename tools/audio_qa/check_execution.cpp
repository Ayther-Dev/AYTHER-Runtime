#include "check_execution.h"

#include "capability_gate.h"
#include "capability_report.h"
#include "control_message.h"
#include "durable_file.h"
#include "fact_batch.h"
#include "incremental_evidence.h"
#include "inherited_channel.h"
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
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ayther::audio_qa {
namespace {

[[nodiscard]] const std::string *material_path(const Reference &reference,
                                               const MaterialRole role) noexcept {
    for (const auto &material : reference.materials) {
        if (material.role == role && material.source_locator.value)
            return &*material.source_locator.value;
    }
    return nullptr;
}

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

[[nodiscard]] const std::string *condition_value(const Reference &reference,
                                                 const std::string_view key) noexcept {
    for (const auto &condition : reference.conditions) {
        if (condition.key == key && condition.value.value)
            return &*condition.value.value;
    }
    return nullptr;
}

void append_value_argument(std::vector<std::wstring> &arguments, const std::wstring_view option,
                           const std::string *const value) {
    if (value == nullptr || value->empty())
        return;
    arguments.emplace_back(option);
    arguments.push_back(wide(*value));
}

[[nodiscard]] std::optional<bool> boolean_condition(const Reference &reference,
                                                    const std::string_view key) noexcept {
    const auto *value = condition_value(reference, key);
    if (value == nullptr)
        return std::nullopt;
    if (*value == "true")
        return true;
    if (*value == "false")
        return false;
    return std::nullopt;
}

} // namespace

CheckExecutionResult execute_check_replay(const CheckOptions &options, const Request &request,
                                          std::string run_id) noexcept {
    try {
        if (request.take_ids.size() != 1U || run_id.empty())
            return CheckExecutionError::result_mismatch;
        const auto loaded =
            load_play_launch_manifest(options.play_manifest, "qa-check-baseline",
                                      "qa-check-execution", "qa-check-play-manifest");
        const auto *reference = std::get_if<Reference>(&loaded);
        if (reference == nullptr)
            return CheckExecutionError::manifest_unavailable;
        const auto stored_reference = read_reference(options.reference);
        const auto *campaign_reference = std::get_if<StoredReference>(&stored_reference);
        if (campaign_reference == nullptr)
            return CheckExecutionError::reference_unavailable;
        const auto *core = material_path(*reference, MaterialRole::core);
        const auto *rom = material_path(*reference, MaterialRole::rom);
        if (core == nullptr || rom == nullptr)
            return CheckExecutionError::manifest_missing_material;

        const auto identified = identify_runtime_binary(options.runtime);
        const auto *runtime = std::get_if<RuntimeBinaryIdentity>(&identified);
        if (runtime == nullptr)
            return CheckExecutionError::runtime_identity_unavailable;

        const auto data_location = runtime_data_location();
        auto prepared_data = prepare_isolated_runtime_data(data_location.user_root,
                                                           std::filesystem::path{options.output} /
                                                               (".runtime-data-" + run_id));
        auto *prepared = std::get_if<IsolatedRuntimeData>(&prepared_data);
        if (prepared == nullptr)
            return CheckExecutionError::runtime_data_isolation_failed;
        auto isolated_data = std::move(*prepared);
        std::vector<RuntimeEnvironmentEntry> environment{
            {data_location.environment_key, isolated_data.directory().wstring()}};
        if (options.presentation == "visible") {
            for (const auto key :
                 {"SystemRoot", "WINDIR", "TEMP", "TMP", "SDL_VIDEO_DRIVER", "SDL_AUDIO_DRIVER",
                  "SDL_AUDIO_FREQUENCY", "SDL_AUDIO_CHANNELS", "SDL_AUDIO_DEVICE_SAMPLE_FRAMES"})
                if (const auto value = environment_path(key))
                    environment.emplace_back(wide(key), value->wstring());
        }
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
        if (options.presentation == "visible" &&
            std::find(offer->capabilities.begin(), offer->capabilities.end(),
                      "visible_replay_v1") == offer->capabilities.end())
            return CheckExecutionError::runtime_incompatible;

        auto control_created = create_inherited_control_channel();
        auto data_created = create_inherited_data_channel();
        auto *control = std::get_if<InheritedControlChannel>(&control_created);
        auto *data = std::get_if<InheritedDataChannel>(&data_created);
        if (control == nullptr || data == nullptr)
            return CheckExecutionError::channel_unavailable;

        const auto encoded = encode_request_message(request, 1U);
        const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
        if (message == nullptr)
            return CheckExecutionError::request_encoding_failed;

        std::vector<std::wstring> arguments{
            L"--qa-session",
            L"--qa-control-channel",
            wide(inherited_data_channel_token(control->runtime_read.get())),
            L"--qa-data-channel",
            wide(inherited_data_channel_token(data->runtime_write.get())),
            L"--qa-run-id",
            wide(run_id),
            L"--core",
            wide(*core),
            L"--rom",
            wide(*rom)};
        if (options.pack_mode == "hd") {
            arguments.emplace_back(L"--pack");
            arguments.push_back(wide(options.pack));
        }
        append_value_argument(arguments, L"--manifest", &options.play_manifest);
        append_value_argument(arguments, L"--qa-presentation", &options.presentation);
        append_value_argument(arguments, L"--profile", condition_value(*reference, "profile"));
        append_value_argument(arguments, L"--subsystems",
                              condition_value(*reference, "subsystems"));
        append_value_argument(arguments, L"--mute-buses",
                              condition_value(*reference, "muted_buses"));
        append_value_argument(arguments, L"--output", condition_value(*reference, "output"));
        append_value_argument(arguments, L"--patch", condition_value(*reference, "patch"));
        if (const auto shaders = boolean_condition(*reference, "shaders"))
            arguments.emplace_back(*shaders ? L"--shaders" : L"--no-shaders");
        for (const auto &condition : campaign_reference->reference.conditions) {
            if (condition.key == "core_option" && condition.value.value) {
                arguments.emplace_back(L"--core-option");
                arguments.push_back(wide(*condition.value.value));
            }
        }
        if (!options.trust_registry.empty()) {
            arguments.emplace_back(L"--trust-registry");
            arguments.push_back(wide(options.trust_registry));
        }
        const std::array<NativeChannelHandle, 2> inherited{control->runtime_read.get(),
                                                           data->runtime_write.get()};
        if (options.presentation == "none")
            environment.insert(environment.end(), {{L"SDL_AUDIO_DRIVER", L"dummy"},
                                                   {L"SDL_AUDIO_FREQUENCY", L"44100"},
                                                   {L"SDL_AUDIO_CHANNELS", L"2"},
                                                   {L"SDL_AUDIO_DEVICE_SAMPLE_FRAMES", L"64"}});
        auto launched =
            launch_runtime_process(*runtime, arguments, std::move(environment), inherited);
        auto *process = std::get_if<OwnedRuntimeProcess>(&launched);
        if (process == nullptr)
            return CheckExecutionError::runtime_launch_failed;
        control->runtime_read.reset();
        data->runtime_write.reset();

        if (!write_channel(control->supervisor_write, *message))
            return CheckExecutionError::request_delivery_failed;
        control->supervisor_write.reset();

        const auto admission_message = read_protocol_message(data->supervisor_read, 2U);
        const auto *admission_bytes = std::get_if<std::vector<std::byte>>(&admission_message);
        if (admission_bytes == nullptr)
            return CheckExecutionError::admission_failed;
        const auto admitted = decode_admission_message(*admission_bytes, 1U);
        const auto *accepted = std::get_if<Request>(&admitted);
        if (accepted == nullptr || accepted->admission != Admission::accepted ||
            accepted->request_id != request.request_id)
            return CheckExecutionError::admission_failed;

        auto opened_evidence = open_incremental_evidence(options.output, run_id);
        auto *evidence_writer = std::get_if<IncrementalEvidenceWriter>(&opened_evidence);
        if (evidence_writer == nullptr)
            return CheckExecutionError::evidence_stream_invalid;

        CheckExecutionEvidence evidence;
        std::uint64_t sequence = 2U;
        for (;;) {
            const auto received = read_protocol_message(data->supervisor_read, 2U);
            const auto *bytes = std::get_if<std::vector<std::byte>>(&received);
            if (bytes == nullptr || bytes->size() < protocol_header_bytes)
                return CheckExecutionError::evidence_stream_invalid;
            const auto header =
                decode_protocol_header(std::span{*bytes}.first(protocol_header_bytes), 2U);
            if (header.error != HeaderError::none || header.header.channel_sequence != sequence)
                return CheckExecutionError::evidence_stream_invalid;
            if (header.header.type == MessageType::fact_batch) {
                const auto decoded = decode_fact_batch(*bytes, sequence);
                const auto *facts = std::get_if<std::vector<Fact>>(&decoded);
                if (facts == nullptr)
                    return CheckExecutionError::evidence_stream_invalid;
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
            } else if (header.header.type == MessageType::terminal) {
                const auto decoded = decode_replay_execution_result(*bytes, sequence);
                const auto *result = std::get_if<ReplayExecutionResult>(&decoded);
                if (result == nullptr)
                    return CheckExecutionError::terminal_failed;
                evidence.replay = *result;
                const auto payload = std::span{*bytes}.subspan(protocol_header_bytes);
                const auto published = publish_durable_file(
                    std::filesystem::path{options.output} / "runs" / run_id / "replay-result.toml",
                    payload);
                if (!std::holds_alternative<DurablePublishedFile>(published))
                    return CheckExecutionError::evidence_stream_invalid;
                break;
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

        std::int32_t exit_code{};
        if (!process->wait(30000U, exit_code))
            return CheckExecutionError::runtime_timeout;
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
