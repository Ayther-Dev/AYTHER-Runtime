#pragma once

#include <cstdint>

namespace ayther::audio_qa {
struct ContractVersion {
    std::uint16_t major{};
    std::uint16_t minor{};
    bool operator==(const ContractVersion &) const = default;
};
inline constexpr ContractVersion engine_contract_version{1, 0};
inline constexpr ContractVersion runtime_protocol_version{1, 0};
inline constexpr ContractVersion evidence_schema_version{1, 0};
inline constexpr ContractVersion hd_state_schema_version{1, 0};
} // namespace ayther::audio_qa
