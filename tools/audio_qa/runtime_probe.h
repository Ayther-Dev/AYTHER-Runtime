#pragma once

#include "field_issue.h"
#include "runtime_process.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::uint32_t runtime_probe_timeout_ms = 15'000;
inline constexpr std::size_t max_runtime_probe_output_bytes = 64U * 1024U;

// Spec 002 (RF-1.7, RF-2.2, plan §5.2, contracts.md C5): what `ayther_runtime
// --probe-core <core> [--probe-rom <rom>]` says about a library, read from the
// `AYTHER_STATUS` probe event it writes. The game fields exist only when a ROM was
// probed: whether the core loaded it and the timing the replay will use.
struct CoreProbe {
    bool loaded{};
    std::string reason;
    std::string library_name;
    std::string library_version;
    std::vector<std::string> extensions;
    std::optional<bool> game_loaded;
    std::optional<double> timing_fps;
    std::string game_message;
    bool operator==(const CoreProbe &) const = default;
};

[[nodiscard]] std::optional<CoreProbe> parse_core_probe(std::string_view output);

// The library must load as a libretro core, declare the extension of the ROM (a core
// that declares none accepts any) and, when the ROM was probed, load it. Codes, always
// in the --core field: core_invalid, core_load_failed, core_platform_mismatch and
// core_rejects_rom.
[[nodiscard]] std::optional<std::string> core_issue(const CoreProbe &probe,
                                                    const std::filesystem::path &rom);

// What the ROM probe loads with the core, so that the timing is the one of the replay.
struct CoreProbeLaunch {
    std::filesystem::path rom;
    std::vector<std::string> core_options;
    std::optional<std::string> patch;
};

using CoreProbeResult = std::variant<CoreProbe, FieldIssue>;

// Runs the probe in its own process: a library that is not a core or that crashes the
// probe never reaches the supervisor. A process that dies without answering means the
// library is not a usable core, or does not load the ROM when one was given; a Runtime
// that cannot run the probe is reported in the --runtime field.
[[nodiscard]] CoreProbeResult probe_core(const RuntimeBinaryIdentity &runtime,
                                         const std::filesystem::path &core,
                                         const std::optional<CoreProbeLaunch> &launch,
                                         std::vector<RuntimeEnvironmentEntry> environment) noexcept;

// Spec 002 (RF-2.2, contracts.md C5, «Sondeo de pack del Runtime»): what
// `ayther_runtime --probe-pack <pack> [--trust-registry R]` says about a pack.
struct PackProbe {
    bool opened{};
    std::string signature;
    std::string trust;
    std::optional<std::uint64_t> poses;
    std::optional<std::uint64_t> audio_events;
    std::vector<std::string> unreadable_assets;
    // D-1 (RF-2.2): the profiles the pack offers; nullopt from a Runtime that does not list
    // them.
    std::optional<std::vector<std::string>> profiles;
    std::string game_id;
    std::vector<std::string> errors;
    std::string reason;
    std::string message;
    bool operator==(const PackProbe &) const = default;
};

[[nodiscard]] std::optional<PackProbe> parse_pack_probe(std::string_view output);

// A pack the Runtime cannot use stops the request with the probe reason, never as
// «Sin pack». A signed pack without a registry, or a registry that cannot be used (D-10),
// points at --trust-registry; the rest at --pack.
[[nodiscard]] std::optional<FieldIssue> pack_issue(const PackProbe &probe);

// D-1 (RF-2.2): a requested profile that the pack does not offer is a mismatch of the
// request, found before admission. Nothing to check without a requested profile or when the
// Runtime did not list the profiles of the pack.
[[nodiscard]] std::optional<FieldIssue> profile_issue(const std::optional<std::string> &profile,
                                                      const PackProbe &probe);

using PackProbeResult = std::variant<PackProbe, FieldIssue>;

[[nodiscard]] PackProbeResult probe_pack(const RuntimeBinaryIdentity &runtime,
                                         const std::filesystem::path &pack,
                                         const std::optional<std::filesystem::path> &trust_registry,
                                         std::vector<RuntimeEnvironmentEntry> environment) noexcept;

} // namespace ayther::audio_qa
