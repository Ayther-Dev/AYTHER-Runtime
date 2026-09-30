#include "capability_gate.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

qa::CapabilitySet valid_offer() {
    qa::CapabilitySet offer;
    for (std::size_t index = 0; index < offer.contracts.size(); ++index) {
        offer.contracts[index] = {qa::supported_versions[index]};
    }
    for (const auto capability : qa::required_capabilities) {
        offer.capabilities.emplace_back(capability);
    }
    offer.limits = qa::required_limits;
    return offer;
}

bool start(void *const context) noexcept {
    ++*static_cast<unsigned *>(context);
    return true;
}

} // namespace

int main() {
    try {
        unsigned starts{};
        auto missing_offer = valid_offer();
        const auto missing = std::find(missing_offer.capabilities.begin(),
                                       missing_offer.capabilities.end(), "bounded_cancel");
        missing_offer.capabilities.erase(missing);
        qa::CapabilityGate missing_gate;
        require(!missing_gate.negotiate(missing_offer) &&
                    missing_gate.state() == qa::CapabilityGateState::rejected &&
                    missing_gate.diagnostic_code() == "required_capability_missing" &&
                    missing_gate.check().capability == "bounded_cancel" &&
                    !missing_gate.start_game(&starts, start) && starts == 0,
                "missing_capability_started_game");

        auto incompatible_offer = valid_offer();
        incompatible_offer.contracts[static_cast<std::size_t>(qa::ContractKind::runtime)] = {
            {2, 0}};
        qa::CapabilityGate incompatible_gate;
        require(!incompatible_gate.negotiate(incompatible_offer) &&
                    incompatible_gate.diagnostic_code() == "capability_version_incompatible" &&
                    incompatible_gate.check().contract == qa::ContractKind::runtime &&
                    !incompatible_gate.start_game(&starts, start) && starts == 0,
                "incompatible_version_started_game");

        qa::CapabilityGate accepted_gate;
        require(accepted_gate.negotiate(valid_offer()) &&
                    accepted_gate.state() == qa::CapabilityGateState::accepted &&
                    accepted_gate.start_game(&starts, start) && starts == 1 &&
                    accepted_gate.state() == qa::CapabilityGateState::game_started &&
                    !accepted_gate.start_game(&starts, start) && starts == 1,
                "compatible_offer_did_not_gate_single_start");

        std::puts("capability_gate_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "capability_gate_test: %s\n", error.what());
        return 1;
    }
}
