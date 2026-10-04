#include "request_registry.h"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

qa::Request request(const std::string &request_id) {
    return {request_id,    "session-112",          "conditions-golden-axe",
            {"take-main"}, qa::Admission::pending, std::nullopt};
}

qa::Run run(const std::string &run_id, const std::string &request_id) {
    qa::Run value;
    value.run_id = run_id;
    value.request_id = request_id;
    value.take_id = "take-main";
    return value;
}

} // namespace

int main() {
    try {
        qa::RequestRegistry registry;
        const auto first_request = request("request-first");
        const auto first =
            registry.register_request(first_request, run("run-first", "request-first"));
        require(first.decision == qa::RequestRegistrationDecision::accepted,
                "first_execution_was_not_accepted");

        auto first_evidence = first.run;
        first_evidence.phase = qa::Phase::closed;
        first_evidence.playback_result = qa::PlaybackResult::natural_end;
        first_evidence.evidence_result = qa::EvidenceResult::complete;
        first_evidence.last_executed_frame = 900;
        first_evidence.last_durable_sample = 48000;
        first_evidence.cessation_confirmed = true;
        require(registry.update_run(first_evidence), "first_evidence_was_not_preserved");

        const auto second = registry.register_request(request("request-repeat"),
                                                      run("run-repeat", "request-repeat"));
        require(second.decision == qa::RequestRegistrationDecision::accepted &&
                    second.run.run_id == "run-repeat" && registry.size() == 2,
                "new_identity_did_not_create_an_independent_execution");

        const auto first_again =
            registry.register_request(first_request, run("run-ignored", "request-first"));
        require(first_again.decision == qa::RequestRegistrationDecision::known &&
                    first_again.run == first_evidence && registry.size() == 2,
                "intentional_repetition_replaced_previous_evidence");

        const auto second_again = registry.register_request(
            request("request-repeat"), run("run-also-ignored", "request-repeat"));
        require(second_again.decision == qa::RequestRegistrationDecision::known &&
                    second_again.run == second.run && registry.size() == 2,
                "intentional_repetition_reused_previous_execution");

        std::puts("intentional_repetition_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "intentional_repetition_test: %s\n", error.what());
        return 1;
    }
}
