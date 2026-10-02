#include "capability_gate.h"
#include "capability_report.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <variant>

namespace qa = ayther::audio_qa;

namespace {

constexpr const char *valid_report =
    "[main] AYTHER Runtime 0.1.0-beta.6; Engine 0.1.0\r\n"
    "AYTHER_QA_CAPABILITIES {\"schema\":\"1.0\","
    "\"runtime_version\":\"0.1.0-beta.6\",\"engine_version\":\"0.1.0\","
    "\"contracts\":{\"engine\":[\"1.0\"],\"runtime\":[\"1.0\"],"
    "\"evidence\":[\"1.0\"],\"hd_state\":[\"1.0\"]},"
    "\"capabilities\":[\"take_replay\",\"pack_inventory\","
    "\"detector_ingress\",\"selection_trace\",\"playback_lifecycle\","
    "\"mixer_contributions\",\"sample_alignment\",\"output_capture\","
    "\"bounded_cancel\",\"trace_integrity\"],"
    "\"limits\":{\"fact_bytes\":65536,\"batch_bytes\":262144,"
    "\"live_occurrences\":256,\"audio_channels\":8,"
    "\"sample_rate\":192000,\"cancel_milliseconds\":2000}}\r\n";

void require(const bool condition, const char *const message) {
    if (!condition)
        throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        const auto decoded = qa::decode_runtime_capability_report(valid_report);
        const auto *offer = std::get_if<qa::CapabilitySet>(&decoded);
        require(offer != nullptr, "canonical report must decode");
        qa::CapabilityGate gate;
        require(gate.negotiate(*offer), "canonical offer must pass the gate");

        const auto missing =
            qa::decode_runtime_capability_report("[main] AYTHER Runtime 0.1.0-beta.3\n");
        require(std::get<qa::CapabilityReportError>(missing) ==
                    qa::CapabilityReportError::marker_missing,
                "legacy output must retain a missing marker diagnostic");

        const std::string duplicate = std::string{valid_report} + valid_report;
        require(std::get<qa::CapabilityReportError>(qa::decode_runtime_capability_report(
                    duplicate)) == qa::CapabilityReportError::duplicate_report,
                "duplicate reports must be rejected");

        std::string malformed = valid_report;
        malformed.replace(malformed.find("\"sample_rate\":192000"), 22U,
                          "\"sample_rate\":\"192000\"");
        require(std::holds_alternative<qa::CapabilityReportError>(
                    qa::decode_runtime_capability_report(malformed)),
                "typed limits must not accept strings");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "capability_report_test_failed: %s\n", error.what());
        return 1;
    }
}
