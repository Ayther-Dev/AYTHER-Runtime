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
// D-10: a trust registry that is missing, unreadable or malformed is a configuration error, as
// for a launch (docs/process-protocol.md, `pack.trust_registry_invalid`, 78).
inline constexpr int pack_probe_registry_invalid_exit_code = 78;
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
    // trusted | untrusted | unverified | unknown (D-10: not evaluated, the registry could not be
    // used or the pack does not open at all)
    std::string trust{"unverified"};
    std::optional<PackCatalog> catalog;
    std::vector<std::string> unreadable_assets;
    // Spec 002 (D-1, RF-2.2): the ids of the profiles the pack offers.
    std::vector<std::string> profiles;
    std::string game_id;
    // Error codes of the Engine validation of the pack.
    std::vector<std::string> errors;
    std::string message;
    // D-10: why the trust registry cannot be used (missing, unreadable, malformed); empty when
    // it can or when none was given.
    std::string trust_registry_error;
};

// Why the pack cannot be used, or empty when it can. A signed pack needs a trust
// registry: without one it is `unverified` and cannot be used.
[[nodiscard]] std::string pack_probe_reason(const PackProbeReport &report);
[[nodiscard]] std::string format_pack_probe_line(const PackProbeReport &report);
// 0 when the pack can be used, 78 when the trust registry cannot, 66 otherwise.
[[nodiscard]] int pack_probe_exit_code(const PackProbeReport &report);

// D-10: what the trusted opening says about trust. Only a pack the Engine refused for trust
// (signature, key, validity, scope, revocation) is untrusted; one that fails to open for
// another reason says nothing about trust.
[[nodiscard]] std::string pack_trust(bool registry_given, bool opened_with_registry,
                                     bool refused_for_trust);

// QA build only (pack_probe_engine.cpp): checks the trust registry as a launch does (D-10),
// then opens and checks the pack with the Engine.
[[nodiscard]] PackProbeReport probe_pack(const std::string &pack,
                                         const std::string &trust_registry);

} // namespace ayther::runtime
