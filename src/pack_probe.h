#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ayther::runtime {

// Spec 002 (contracts.md C5, «Sondeo de pack del Runtime»; RF-2.2): what the QA build
// says about a pack before any game starts. `ayther_runtime --probe-pack <pack>
// [--trust-registry <registry>]` writes one `AYTHER_PACK_PROBE` line and ends with 0
// when the pack can be used and 66, with the reason, when it cannot.
inline constexpr int pack_probe_unusable_exit_code = 66;
inline constexpr std::string_view pack_probe_prefix = "AYTHER_PACK_PROBE ";

struct PackCatalog {
    std::uint32_t poses{};
    std::uint32_t audio_events{};
    // Every asset the catalogs name, once, in order of appearance.
    std::vector<std::string> assets;
    bool operator==(const PackCatalog &) const = default;
};

// Reads the pose and audio catalogs of a pack; an absent catalog is empty text. nullopt
// when either is not TOML of the expected shape.
[[nodiscard]] std::optional<PackCatalog> read_pack_catalog(std::string_view poses,
                                                           std::string_view audio_events);

// False when bytes that announce a PNG, a JPEG or a WAV file cannot be decoded. Other
// formats only need to be readable, which the Engine verifies against the pack index.
[[nodiscard]] bool asset_decodes(std::span<const std::uint8_t> bytes) noexcept;

struct PackProbeReport {
    bool opened{};
    bool signed_pack{};
    // valid | invalid | unsigned | unverified
    std::string signature{"unverified"};
    // trusted | untrusted | unverified
    std::string trust{"unverified"};
    std::optional<PackCatalog> catalog;
    std::vector<std::string> unreadable_assets;
    std::string game_id;
    // Error codes of the Engine validation of the pack.
    std::vector<std::string> errors;
    std::string message;
};

// Why the pack cannot be used, or empty when it can. A signed pack needs a trust
// registry: without one it is `unverified` and cannot be used.
[[nodiscard]] std::string pack_probe_reason(const PackProbeReport &report);
[[nodiscard]] std::string format_pack_probe_line(const PackProbeReport &report);

// QA build only (pack_probe_engine.cpp): opens and checks the pack with the Engine.
[[nodiscard]] PackProbeReport probe_pack(const std::string &pack,
                                         const std::string &trust_registry);

} // namespace ayther::runtime
