#include "runtime_probe.h"

#include "json_fields.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace ayther::audio_qa {
namespace {

constexpr std::string_view status_prefix = "AYTHER_STATUS ";

std::string lowercase(std::string_view text) {
    std::string result{text};
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

std::vector<std::string> split_extensions(std::string_view text) {
    std::vector<std::string> extensions;
    while (!text.empty()) {
        const auto separator = text.find('|');
        const auto extension = text.substr(0, separator);
        if (!extension.empty())
            extensions.push_back(lowercase(extension));
        if (separator == std::string_view::npos)
            break;
        text.remove_prefix(separator + 1U);
    }
    return extensions;
}

// The last line that carries the given prefix; the probes write other lines before it.
std::optional<std::string_view> last_line_with(std::string_view output, std::string_view prefix) {
    std::optional<std::string_view> found;
    while (!output.empty()) {
        const auto end = output.find('\n');
        auto line = output.substr(0, end);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.starts_with(prefix))
            found = line.substr(prefix.size());
        if (end == std::string_view::npos)
            break;
        output.remove_prefix(end + 1U);
    }
    return found;
}

std::wstring wide(const std::string &text) { return std::filesystem::path{text}.wstring(); }

} // namespace

std::optional<CoreProbe> parse_core_probe(std::string_view output) {
    const auto line = last_line_with(output, status_prefix);
    if (!line)
        return std::nullopt;
    const auto fields = parse_json_fields(*line);
    if (!fields || fields->string("event") != "probe")
        return std::nullopt;
    const auto loaded = fields->boolean("ok");
    if (!loaded)
        return std::nullopt;
    CoreProbe probe;
    probe.loaded = *loaded;
    if (!probe.loaded) {
        probe.reason = fields->string("reason").value_or(std::string{});
        return probe;
    }
    probe.library_name = fields->string("library_name").value_or(std::string{});
    probe.library_version = fields->string("library_version").value_or(std::string{});
    probe.extensions = split_extensions(fields->string("valid_extensions").value_or(std::string{}));
    probe.game_loaded = fields->boolean("game_loaded");
    probe.timing_fps = fields->number("timing_fps");
    probe.game_message = fields->string("game_message").value_or(std::string{});
    return probe;
}

std::optional<std::string> core_issue(const CoreProbe &probe, const std::filesystem::path &rom) {
    if (!probe.loaded)
        return probe.reason == "core.load_failed" ? "core_load_failed" : "core_invalid";
    if (!probe.extensions.empty()) {
        auto extension = lowercase(rom.extension().string());
        if (!extension.empty() && extension.front() == '.')
            extension.erase(extension.begin());
        if (std::find(probe.extensions.begin(), probe.extensions.end(), extension) ==
            probe.extensions.end())
            return "core_platform_mismatch";
    }
    if (probe.game_loaded == false)
        return "core_rejects_rom";
    return std::nullopt;
}

CoreProbeResult probe_core(const RuntimeBinaryIdentity &runtime, const std::filesystem::path &core,
                           const std::optional<CoreProbeLaunch> &launch,
                           std::vector<RuntimeEnvironmentEntry> environment) noexcept {
    try {
        std::vector<std::wstring> arguments{L"--probe-core", core.wstring()};
        if (launch) {
            arguments.emplace_back(L"--probe-rom");
            arguments.push_back(launch->rom.wstring());
            for (const auto &option : launch->core_options) {
                arguments.emplace_back(L"--core-option");
                arguments.push_back(wide(option));
            }
            if (launch->patch && !launch->patch->empty()) {
                arguments.emplace_back(L"--patch");
                arguments.push_back(wide(*launch->patch));
            }
        }
        const auto queried =
            query_runtime_process(runtime, arguments, std::move(environment),
                                  max_runtime_probe_output_bytes, runtime_probe_timeout_ms);
        const auto *output = std::get_if<RuntimeQueryOutput>(&queried);
        if (output == nullptr)
            return FieldIssue{"--runtime", "runtime_probe_failed"};
        if (auto probe = parse_core_probe(output->output)) {
            // A Runtime without the ROM probe answers as 1.0 and says nothing of the game.
            if (launch && probe->loaded && !probe->game_loaded)
                return FieldIssue{"--runtime", "runtime_rom_probe_unsupported"};
            return std::move(*probe);
        }
        // The probe process died before answering: the library is not a usable core, or
        // it crashed while loading the ROM.
        return FieldIssue{"--core", launch ? "core_rejects_rom" : "core_invalid"};
    } catch (...) {
        return FieldIssue{"--runtime", "runtime_probe_failed"};
    }
}

std::optional<PackProbe> parse_pack_probe(std::string_view output) {
    const auto line = last_line_with(output, "AYTHER_PACK_PROBE ");
    if (!line)
        return std::nullopt;
    const auto fields = parse_json_fields(*line);
    if (!fields || fields->string("schema") != "1.0")
        return std::nullopt;
    const auto opened = fields->boolean("opened");
    const auto signature = fields->string("signature");
    const auto trust = fields->string("trust");
    if (!opened || !signature || !trust)
        return std::nullopt;
    PackProbe probe;
    probe.opened = *opened;
    probe.signature = *signature;
    probe.trust = *trust;
    probe.poses = fields->unsigned_number("catalog.poses");
    probe.audio_events = fields->unsigned_number("catalog.audio_events");
    const auto read_list = [&fields](std::string_view path, std::vector<std::string> &values) {
        const auto count = fields->size(path);
        for (std::size_t index = 0; count && index < *count; ++index) {
            std::string element{path};
            element += '[';
            element += std::to_string(index);
            element += ']';
            if (auto value = fields->string(element))
                values.push_back(std::move(*value));
        }
    };
    read_list("unreadable_assets", probe.unreadable_assets);
    read_list("errors", probe.errors);
    probe.game_id = fields->string("game_id").value_or(std::string{});
    probe.reason = fields->string("reason").value_or(std::string{});
    probe.message = fields->string("message").value_or(std::string{});
    return probe;
}

std::optional<FieldIssue> pack_issue(const PackProbe &probe) {
    if (probe.reason.empty())
        return std::nullopt;
    if (probe.reason == "pack_trust_unverified")
        return FieldIssue{"--trust-registry", probe.reason};
    return FieldIssue{"--pack", probe.reason};
}

PackProbeResult probe_pack(const RuntimeBinaryIdentity &runtime, const std::filesystem::path &pack,
                           const std::optional<std::filesystem::path> &trust_registry,
                           std::vector<RuntimeEnvironmentEntry> environment) noexcept {
    try {
        std::vector<std::wstring> arguments{L"--probe-pack", pack.wstring()};
        if (trust_registry && !trust_registry->empty()) {
            arguments.emplace_back(L"--trust-registry");
            arguments.push_back(trust_registry->wstring());
        }
        const auto queried =
            query_runtime_process(runtime, arguments, std::move(environment),
                                  max_runtime_probe_output_bytes, runtime_probe_timeout_ms);
        const auto *output = std::get_if<RuntimeQueryOutput>(&queried);
        if (output == nullptr)
            return FieldIssue{"--runtime", "runtime_probe_failed"};
        auto probe = parse_pack_probe(output->output);
        // A Runtime without the pack probe, or one that died probing: never «Sin pack».
        if (!probe)
            return FieldIssue{"--pack", "pack_probe_unavailable"};
        // The exit code and the reason must agree; a usable pack ends with 0.
        if (probe->reason.empty() != (output->exit_code == 0))
            return FieldIssue{"--pack", "pack_probe_unavailable"};
        return std::move(*probe);
    } catch (...) {
        return FieldIssue{"--runtime", "runtime_probe_failed"};
    }
}

} // namespace ayther::audio_qa
