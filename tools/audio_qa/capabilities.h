#pragma once

#include "contract_version.h"
#include "model_limits.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::array<std::string_view, 10> required_capabilities{
    "take_replay",        "pack_inventory",      "detector_ingress", "selection_trace",
    "playback_lifecycle", "mixer_contributions", "sample_alignment", "output_capture",
    "bounded_cancel",     "trace_integrity"};
enum class ContractKind : std::size_t { engine, runtime, evidence, hd_state };
inline constexpr std::array supported_versions{engine_contract_version, runtime_protocol_version,
                                               evidence_schema_version, hd_state_schema_version};

struct CapabilityLimits {
    std::uint32_t fact_bytes{};
    std::uint32_t batch_bytes{};
    std::uint32_t live_occurrences{};
    std::uint32_t audio_channels{};
    std::uint32_t sample_rate{};
    std::uint32_t cancel_milliseconds{};
};
inline constexpr CapabilityLimits required_limits{64 * 1024, 256 * 1024, 256, 8, 192000, 2000};

struct CapabilitySet {
    std::array<std::vector<ContractVersion>, 4> contracts;
    std::vector<std::string> capabilities;
    CapabilityLimits limits;
};

enum class CapabilityError {
    none,
    malformed_offer,
    incompatible_version,
    missing_capability,
    insufficient_limits
};
struct CapabilityCheck {
    CapabilityError error{CapabilityError::none};
    std::optional<ContractKind> contract;
    std::string capability;
    std::optional<std::array<ContractVersion, 4>> negotiated_versions;
    std::optional<CapabilityLimits> negotiated_limits;
};

// Validates an offer; it does not assert that a binary implements or has passed these capabilities.
[[nodiscard]] CapabilityCheck validate_capabilities(const CapabilitySet &offer);

} // namespace ayther::audio_qa
