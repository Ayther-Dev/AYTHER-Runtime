#include "take_process_sequence.h"
#include "take_run_sequence.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        const qa::Request request{"request-127",
                                  "session-127",
                                  "conditions-127",
                                  {"take-main", "take-complementary"},
                                  qa::Admission::accepted};
        qa::TakeRunSequence selected{request, {"run-main", "run-complementary"}};
        require(selected.error() == qa::TakeRunSequenceError::none,
                "selected_take_sequence_was_invalid");

        qa::TakeProcessSequence processes{selected.runs()};
        const auto first = processes.start_next("process-instance-1");
        require(first.has_value() && first->take_index == 0 && first->run.run_id == "run-main" &&
                    first->run.take_id == "take-main" && processes.active(),
                "main_take_did_not_start_first");
        require(!processes.start_next("process-instance-2").has_value(),
                "complementary_take_started_while_main_process_was_active");

        auto failed = first->run;
        failed.phase = qa::Phase::closed;
        failed.playback_result = qa::PlaybackResult::error;
        failed.evidence_result = qa::EvidenceResult::incomplete;
        failed.last_executed_frame = 44;
        failed.last_durable_sample = 35200;
        require(!processes.finish_current(failed) && processes.active() &&
                    !processes.start_next("process-instance-2").has_value(),
                "unconfirmed_failed_process_allowed_next_take");

        failed.cessation_confirmed = true;
        require(processes.finish_current(failed) && !processes.active() &&
                    processes.completed_count() == 1,
                "confirmed_failed_process_did_not_complete_first_take");
        require(!processes.start_next("process-instance-1").has_value(),
                "previous_process_instance_was_reused");

        const auto second = processes.start_next("process-instance-2");
        require(second.has_value() && second->take_index == 1 &&
                    second->run.run_id == "run-complementary" &&
                    second->run.take_id == "take-complementary" &&
                    second->process_instance_id == "process-instance-2" &&
                    processes.runs()[0] == failed,
                "complementary_take_did_not_start_in_a_new_process");

        std::puts("take_process_sequence_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "take_process_sequence_test: %s\n", error.what());
        return 1;
    }
}
