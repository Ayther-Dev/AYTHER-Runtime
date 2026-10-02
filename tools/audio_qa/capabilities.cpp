#include "capabilities.h"

#include <algorithm>

namespace ayther::audio_qa {
CapabilityCheck validate_capabilities(const CapabilitySet &offer) {
    CapabilityCheck result;
    if (offer.capabilities.size() > max_capability_names) {
        result.error = CapabilityError::malformed_offer;
        return result;
    }
    for (std::size_t i = 0; i < offer.capabilities.size(); ++i) {
        const auto &name = offer.capabilities[i];
        if (name.empty() || name.size() > max_identity_bytes) {
            result.error = CapabilityError::malformed_offer;
            return result;
        }
        for (std::size_t before = 0; before < i; ++before) {
            if (offer.capabilities[before] == name) {
                result.error = CapabilityError::malformed_offer;
                return result;
            }
        }
    }
    for (std::size_t contract = 0; contract < offer.contracts.size(); ++contract) {
        const auto &versions = offer.contracts[contract];
        if (versions.empty() || versions.size() > max_contract_versions) {
            result.error = CapabilityError::malformed_offer;
            result.contract = static_cast<ContractKind>(contract);
            return result;
        }
        for (std::size_t i = 0; i < versions.size(); ++i) {
            for (std::size_t previous = 0; previous < i; ++previous) {
                if (versions[i] == versions[previous]) {
                    result.error = CapabilityError::malformed_offer;
                    result.contract = static_cast<ContractKind>(contract);
                    return result;
                }
            }
        }
        if (std::find(versions.begin(), versions.end(), supported_versions[contract]) ==
            versions.end()) {
            result.error = CapabilityError::incompatible_version;
            result.contract = static_cast<ContractKind>(contract);
            return result;
        }
    }
    for (const auto required : required_capabilities) {
        if (std::find(offer.capabilities.begin(), offer.capabilities.end(), required) ==
            offer.capabilities.end()) {
            result.error = CapabilityError::missing_capability;
            result.capability = required;
            return result;
        }
    }
    const auto &limits = offer.limits;
    if (limits.fact_bytes < required_limits.fact_bytes ||
        limits.batch_bytes < required_limits.batch_bytes ||
        limits.live_occurrences < required_limits.live_occurrences ||
        limits.audio_channels < required_limits.audio_channels ||
        limits.sample_rate < required_limits.sample_rate || limits.cancel_milliseconds == 0 ||
        limits.cancel_milliseconds > required_limits.cancel_milliseconds) {
        result.error = CapabilityError::insufficient_limits;
        return result;
    }
    result.negotiated_versions = supported_versions;
    result.negotiated_limits = required_limits;
    return result;
}
} // namespace ayther::audio_qa
