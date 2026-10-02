#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ayther::audio_qa {

enum class Origin {
    unknown,
    user_declaration,
    configuration,
    artifact_manifest,
    content_measurement,
    runtime_observation
};

struct Provenance {
    Origin origin{Origin::unknown};
    std::string evidence_id;
    bool operator==(const Provenance &) const = default;
};

template <class T> struct Recorded {
    std::optional<T> value;
    Provenance provenance;
    bool operator==(const Recorded &) const = default;
};

struct ContentIdentity {
    std::array<std::uint8_t, 32> sha256{};
    std::uint64_t byte_size{};
    bool operator==(const ContentIdentity &) const = default;
};

enum class MaterialRole { rom, core, pack, take, external_asset, engine_build, runtime_build };
enum class Stability { unverified, verified, changed };

struct Material {
    std::string material_id;
    MaterialRole role{MaterialRole::take};
    Recorded<std::string> source_locator;
    Recorded<std::string> format_version;
    Recorded<ContentIdentity> identified;
    Recorded<ContentIdentity> consumed;
    Stability stability{Stability::unverified};
    Provenance stability_provenance;
    bool operator==(const Material &) const = default;
};

struct BuildReference {
    Recorded<ContentIdentity> artifact;
    Recorded<std::string> release;
    Recorded<std::string> commit;
    Recorded<std::string> variant;
    Recorded<std::string> abi;
    bool operator==(const BuildReference &) const = default;
};

enum class ReferenceRole { initial, extended };

struct EffectiveCondition {
    std::string key;
    Recorded<std::string> value;
    bool operator==(const EffectiveCondition &) const = default;
};

struct Reference {
    std::string baseline_id;
    std::string execution_reference_id;
    ReferenceRole role{ReferenceRole::initial};
    BuildReference engine;
    BuildReference runtime;
    Recorded<std::string> conditions_manifest_id;
    std::vector<EffectiveCondition> conditions;
    std::vector<Material> materials;
    std::vector<Recorded<std::string>> declared_differences;
    bool operator==(const Reference &) const = default;
};

// Structural consistency only: these functions never read files or verify a provenance claim.
[[nodiscard]] bool well_formed(const Material &material) noexcept;
[[nodiscard]] bool well_formed(const Reference &reference) noexcept;

} // namespace ayther::audio_qa
