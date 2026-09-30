#include "capabilities.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
qa::CapabilitySet valid_offer() {
    qa::CapabilitySet offer;
    for (std::size_t i = 0; i < offer.contracts.size(); ++i) {
        offer.contracts[i].push_back(qa::supported_versions[i]);
    }
    for (const auto name : qa::required_capabilities) {
        offer.capabilities.emplace_back(name);
    }
    offer.limits = qa::required_limits;
    return offer;
}
} // namespace
int main() {
    try {
        auto offer = valid_offer();
        const auto accepted = qa::validate_capabilities(offer);
        require(accepted.error == qa::CapabilityError::none &&
                    accepted.negotiated_versions == qa::supported_versions,
                "supported_offer_rejected");
        for (std::size_t i = 0; i < qa::required_capabilities.size(); ++i) {
            auto missing = offer;
            missing.capabilities.erase(missing.capabilities.begin() +
                                       static_cast<std::ptrdiff_t>(i));
            const auto result = qa::validate_capabilities(missing);
            require(result.error == qa::CapabilityError::missing_capability &&
                        result.capability == qa::required_capabilities[i] &&
                        !result.negotiated_versions,
                    "required_capability_omission_accepted");
        }
        for (std::size_t i = 0; i < offer.contracts.size(); ++i) {
            for (const qa::ContractVersion version : {qa::ContractVersion{2, 0}, {1, 1}}) {
                auto incompatible = offer;
                incompatible.contracts[i] = {version};
                const auto result = qa::validate_capabilities(incompatible);
                require(result.error == qa::CapabilityError::incompatible_version &&
                            result.contract == static_cast<qa::ContractKind>(i),
                        "incompatible_contract_accepted");
            }
        }
        offer.contracts[0].push_back({2, 0});
        require(qa::validate_capabilities(offer).negotiated_versions == qa::supported_versions,
                "explicit_common_version_not_selected");
        offer = valid_offer();
        --offer.limits.live_occurrences;
        require(qa::validate_capabilities(offer).error == qa::CapabilityError::insufficient_limits,
                "insufficient_capacity_accepted");
        offer = valid_offer();
        ++offer.limits.cancel_milliseconds;
        require(qa::validate_capabilities(offer).error == qa::CapabilityError::insufficient_limits,
                "slow_cancellation_accepted");
        offer = valid_offer();
        while (offer.capabilities.size() < qa::max_capability_names) {
            offer.capabilities.push_back("optional-" + std::to_string(offer.capabilities.size()));
        }
        require(qa::validate_capabilities(offer).error == qa::CapabilityError::none,
                "optional_capability_boundary_rejected");
        offer.capabilities.push_back("one-too-many");
        require(qa::validate_capabilities(offer).error == qa::CapabilityError::malformed_offer,
                "capability_count_limit_ignored");
        offer = valid_offer();
        for (std::size_t i = 1; i < qa::max_contract_versions; ++i) {
            offer.contracts[0].push_back({static_cast<std::uint16_t>(i + 1), 0});
        }
        require(qa::validate_capabilities(offer).error == qa::CapabilityError::none,
                "version_count_boundary_rejected");
        offer.contracts[0].push_back({100, 0});
        require(qa::validate_capabilities(offer).error == qa::CapabilityError::malformed_offer,
                "version_count_limit_ignored");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "qa_capabilities_test_failed: %s\n", error.what());
        return 1;
    }
}
