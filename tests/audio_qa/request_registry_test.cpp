#include "request_registry.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

qa::Request request() {
    return {"request-111",
            "session-111",
            "conditions-a",
            {"take-main", "take-extra"},
            qa::Admission::pending};
}

qa::Run run() {
    qa::Run value;
    value.run_id = "run-111";
    value.request_id = "request-111";
    value.take_id = "take-main";
    return value;
}

} // namespace

int main() {
    try {
        qa::RequestRegistry registry;
        const auto accepted = registry.register_request(request(), run());
        require(accepted.decision == qa::RequestRegistrationDecision::accepted &&
                    accepted.run.run_id == "run-111" && registry.size() == 1,
                "first_request_was_not_registered");

        auto current = accepted.run;
        current.phase = qa::Phase::playing;
        current.playback_result = qa::PlaybackResult::in_progress;
        require(registry.update_run(current), "known_run_was_not_updated");

        const auto resent = registry.register_request(request(), run());
        require(resent.decision == qa::RequestRegistrationDecision::known &&
                    resent.run == current && registry.size() == 1,
                "identical_resend_created_a_second_execution");

        auto conflict = request();
        conflict.conditions_id = "conditions-b";
        const auto rejected = registry.register_request(conflict, run());
        require(rejected.decision == qa::RequestRegistrationDecision::identity_conflict &&
                    rejected.run == current && registry.size() == 1,
                "different_content_reused_request_identity");

        std::puts("request_registry_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "request_registry_test: %s\n", error.what());
        return 1;
    }
}
