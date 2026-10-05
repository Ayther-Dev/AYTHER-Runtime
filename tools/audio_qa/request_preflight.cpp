#include "request_preflight.h"

#include "capability_gate.h"
#include "capability_report.h"
#include "recording_header.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <random>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

namespace ayther::audio_qa {
namespace {

std::optional<std::wstring> environment_value(const char *name) {
#ifdef _WIN32
    char *raw{};
    std::size_t size{};
    if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr || size <= 1U) {
        std::free(raw);
        return std::nullopt;
    }
    const std::unique_ptr<char, decltype(&std::free)> value{raw, &std::free};
    return std::filesystem::path{value.get()}.wstring();
#else
    const auto *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
        return std::nullopt;
    return std::filesystem::path{value}.wstring();
#endif
}

std::wstring wide(std::string_view text) { return std::filesystem::path{text}.wstring(); }

// The probes load the core, the ROM and the pack in a Runtime process. Its data root is
// a private temporary directory, so the probes never touch the user's Runtime data or
// the destination of the request.
class ProbeDataRoot final {
  public:
    ProbeDataRoot() {
        std::random_device random;
        std::error_code error;
        for (int attempt = 0; attempt < 8 && path_.empty(); ++attempt) {
            auto candidate = std::filesystem::temp_directory_path(error);
            if (error)
                return;
            candidate /= "ayther-qa-probe-" + std::to_string(random()) + std::to_string(random());
            if (std::filesystem::create_directory(candidate, error) && !error)
                path_ = std::move(candidate);
        }
    }
    ~ProbeDataRoot() {
        std::error_code ignored;
        if (!path_.empty())
            std::filesystem::remove_all(path_, ignored);
    }
    ProbeDataRoot(const ProbeDataRoot &) = delete;
    ProbeDataRoot &operator=(const ProbeDataRoot &) = delete;

    [[nodiscard]] std::vector<RuntimeEnvironmentEntry> environment() const {
        std::vector<RuntimeEnvironmentEntry> entries;
#ifdef _WIN32
        if (!path_.empty())
            entries.emplace_back(L"APPDATA", path_.wstring());
#else
        if (!path_.empty())
            entries.emplace_back(L"XDG_DATA_HOME", path_.wstring());
#endif
        for (const auto *key : {"SystemRoot", "WINDIR", "TEMP", "TMP"})
            if (auto value = environment_value(key))
                entries.emplace_back(wide(key), std::move(*value));
        return entries;
    }

  private:
    std::filesystem::path path_;
};

void add_issue(std::vector<FieldIssue> &issues, FieldIssue issue) {
    if (std::find(issues.begin(), issues.end(), issue) == issues.end())
        issues.push_back(std::move(issue));
}

std::optional<MaterialPin> pinned(std::vector<FieldIssue> &issues, MaterialPinResult result) {
    if (const auto *issue = std::get_if<MaterialIssue>(&result)) {
        add_issue(issues, field_issue(*issue));
        return std::nullopt;
    }
    return std::get<MaterialPin>(std::move(result));
}

} // namespace

std::vector<MaterialPin> material_pins(const PreflightPins &pins) {
    std::vector<MaterialPin> materials{pins.runtime, pins.rom, pins.core};
    materials.insert(materials.end(), pins.takes.begin(), pins.takes.end());
    if (pins.pack)
        materials.push_back(*pins.pack);
    if (pins.trust_registry)
        materials.push_back(*pins.trust_registry);
    return materials;
}

std::vector<MaterialPin> take_material_pins(const PreflightPins &pins, std::size_t position) {
    std::vector<MaterialPin> materials{pins.runtime, pins.rom, pins.core};
    const auto take =
        std::find_if(pins.takes.begin(), pins.takes.end(),
                     [position](const auto &pin) { return pin.position == position; });
    if (take != pins.takes.end())
        materials.push_back(*take);
    if (pins.pack)
        materials.push_back(*pins.pack);
    if (pins.trust_registry)
        materials.push_back(*pins.trust_registry);
    return materials;
}

std::string take_field(std::size_t position) {
    std::string field{"--take["};
    field += std::to_string(position);
    field += ']';
    return field;
}

RequestPreflight preflight_request(const EffectiveRequest &request) {
    RequestPreflight result;
    auto &issues = result.issues;

    // Every material: a readable regular file, pinned by content and file identity.
    auto runtime =
        pinned(issues, pin_material(MaterialPinRole::runtime, "--runtime", request.runtime));
    auto rom = pinned(issues, pin_material(MaterialPinRole::rom, "--rom", request.rom));
    auto core = pinned(issues, pin_material(MaterialPinRole::core, "--core", request.core));
    std::optional<MaterialPin> trust_registry;
    bool trust_registry_valid = true;
    if (!request.trust_registry.empty()) {
        trust_registry = pinned(issues, pin_material(MaterialPinRole::trust_registry,
                                                     "--trust-registry", request.trust_registry));
        trust_registry_valid = trust_registry.has_value();
    }
    // `--pack-mode original` keeps the pack unloaded (RNF-6): it is neither pinned nor probed.
    const bool pack_used = request.pack.has_value() && request.pack_mode == "hd";
    std::optional<MaterialPin> pack;
    if (pack_used)
        pack = pinned(issues, pin_material(MaterialPinRole::pack, "--pack", *request.pack));

    // Each take is validated from the bytes it was pinned with.
    std::vector<MaterialPin> takes;
    std::vector<TakeFacts> take_facts;
    for (std::size_t position = 0; position < request.takes.size(); ++position) {
        auto content = pin_material_content(MaterialPinRole::take, take_field(position),
                                            request.takes[position], position, max_recording_bytes);
        if (const auto *issue = std::get_if<MaterialIssue>(&content)) {
            add_issue(issues, field_issue(*issue));
            continue;
        }
        auto &pinned_take = std::get<PinnedContent>(content);
        auto inspection = inspect_take(pinned_take.bytes);
        if (const auto *code = std::get_if<std::string>(&inspection)) {
            add_issue(issues, {take_field(position), *code});
            continue;
        }
        // D-2 (RF-2.2): a damaged initial state is found here, before admission, not when the
        // Runtime restores it.
        if (auto state = take_state_issue(pinned_take.bytes)) {
            add_issue(issues, {take_field(position), std::move(*state)});
            continue;
        }
        takes.push_back(std::move(pinned_take.pin));
        take_facts.push_back(std::get<TakeFacts>(std::move(inspection)));
    }

    // The Runtime probes the core with the ROM and the pack (contracts.md C5).
    std::optional<RuntimeBinaryIdentity> identity;
    if (runtime) {
        const auto identified = identify_runtime_binary(request.runtime);
        if (const auto *binary = std::get_if<RuntimeBinaryIdentity>(&identified))
            identity = *binary;
        else
            add_issue(issues, {"--runtime", "runtime_identity_unavailable"});
    }
    const ProbeDataRoot data_root;
    // C1: the Runtime offers the capabilities and protocol the presentation needs.
    std::optional<ContractVersion> protocol;
    if (identity) {
        const auto queried = query_runtime_process(*identity, {L"--qa-capabilities"},
                                                   data_root.environment(), 64U * 1024U, 15'000U);
        const auto *output = std::get_if<RuntimeQueryOutput>(&queried);
        const auto decoded = output != nullptr && output->exit_code == 0
                                 ? decode_runtime_capability_report(output->output)
                                 : CapabilityReportResult{CapabilityReportError::malformed_report};
        const auto *offer = std::get_if<CapabilitySet>(&decoded);
        CapabilityGate gate;
        if (offer == nullptr || !gate.negotiate(*offer)) {
            add_issue(issues, {"--runtime", "runtime_incompatible"});
        } else {
            auto chosen = negotiate_runtime_protocol(*offer, request.presentation);
            if (auto *issue = std::get_if<FieldIssue>(&chosen))
                add_issue(issues, std::move(*issue));
            else
                protocol = std::get<ContractVersion>(chosen);
        }
    }
    std::optional<CoreProbe> core_probe;
    if (identity && core && rom) {
        const CoreProbeLaunch launch{request.rom, request.conditions.core_options,
                                     request.conditions.patch};
        auto probed = probe_core(*identity, request.core, launch, data_root.environment());
        if (auto *issue = std::get_if<FieldIssue>(&probed)) {
            add_issue(issues, std::move(*issue));
        } else {
            core_probe = std::get<CoreProbe>(std::move(probed));
            if (auto code = core_issue(*core_probe, request.rom))
                add_issue(issues, {"--core", std::move(*code)});
        }
    }
    std::optional<PackProbe> pack_probe;
    if (identity && pack && trust_registry_valid) {
        const std::optional<std::filesystem::path> registry =
            request.trust_registry.empty()
                ? std::nullopt
                : std::optional<std::filesystem::path>{request.trust_registry};
        auto probed = probe_pack(*identity, *request.pack, registry, data_root.environment());
        if (auto *probe_issue = std::get_if<FieldIssue>(&probed)) {
            add_issue(issues, std::move(*probe_issue));
        } else {
            pack_probe = std::get<PackProbe>(std::move(probed));
            if (auto unusable = pack_issue(*pack_probe))
                add_issue(issues, std::move(*unusable));
            else if (auto profile = profile_issue(request.conditions.profile, *pack_probe))
                add_issue(issues, std::move(*profile));
        }
    }

    // The takes and the pack must belong to the ROM, and each take must fit in 900 s at
    // the timing the core reports for that ROM.
    const bool usable_pack = pack_probe && pack_probe->reason.empty();
    if (rom) {
        if (usable_pack)
            if (auto code = pack_game_issue(pack_probe->game_id, rom->crc32))
                add_issue(issues, {"--pack", std::move(*code)});
        for (std::size_t index = 0; index < take_facts.size(); ++index) {
            const auto pack_game =
                usable_pack ? std::optional<std::string_view>{pack_probe->game_id} : std::nullopt;
            if (auto code = take_game_issue(take_facts[index].game_id, rom->crc32, pack_game))
                add_issue(issues, {take_field(takes[index].position), std::move(*code)});
        }
    }
    if (core_probe && !core_issue(*core_probe, request.rom)) {
        for (std::size_t index = 0; index < take_facts.size(); ++index) {
            if (auto issue =
                    take_duration_issue(take_facts[index].frame_count, core_probe->timing_fps)) {
                if (issue->field == "--take")
                    issue->field = take_field(takes[index].position);
                add_issue(issues, std::move(*issue));
            }
        }
    }

    if (!issues.empty())
        return result;
    result.pins = PreflightPins{std::move(*runtime),
                                std::move(*rom),
                                std::move(*core),
                                std::move(takes),
                                std::move(take_facts),
                                std::move(pack),
                                std::move(trust_registry),
                                std::move(*identity),
                                std::move(*core_probe),
                                std::move(pack_probe),
                                *protocol};
    return result;
}

} // namespace ayther::audio_qa
