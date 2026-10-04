#include "take_run_sequence.h"

#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

qa::Request request(std::vector<std::string> take_ids) {
    return {"request-113",       "session-113",           "conditions-golden-axe",
            std::move(take_ids), qa::Admission::accepted, std::nullopt};
}

} // namespace

int main() {
    try {
        qa::TakeRunSequence main_only{request({"take-main"}), {"run-main"}};
        require(main_only.error() == qa::TakeRunSequenceError::none &&
                    main_only.runs().size() == 1 && main_only.runs().front().take_id == "take-main",
                "unselected_complementary_take_was_scheduled");

        qa::TakeRunSequence selected{request({"take-main", "take-complementary"}),
                                     {"run-main", "run-complementary"}};
        require(selected.error() == qa::TakeRunSequenceError::none && selected.runs().size() == 2 &&
                    selected.runs()[0].take_id == "take-main" &&
                    selected.runs()[0].run_id == "run-main" &&
                    selected.runs()[1].take_id == "take-complementary" &&
                    selected.runs()[1].run_id == "run-complementary",
                "selected_take_order_was_not_preserved");

        auto first_result = selected.runs().front();
        first_result.phase = qa::Phase::closed;
        first_result.playback_result = qa::PlaybackResult::natural_end;
        first_result.evidence_result = qa::EvidenceResult::complete;
        first_result.last_executed_frame = 600;
        require(selected.update(first_result) && selected.runs().front() == first_result &&
                    selected.runs()[1].phase == qa::Phase::preparing &&
                    selected.runs()[1].evidence_result == qa::EvidenceResult::pending,
                "take_results_were_not_independent");

        qa::TakeRunSequence mismatched{request({"take-main", "take-extra"}), {"run-main"}};
        qa::TakeRunSequence duplicate_take{request({"take-main", "take-main"}), {"run-a", "run-b"}};
        qa::TakeRunSequence duplicate_run{request({"take-main", "take-extra"}), {"run-a", "run-a"}};
        require(mismatched.error() == qa::TakeRunSequenceError::run_count_mismatch &&
                    duplicate_take.error() == qa::TakeRunSequenceError::duplicate_take &&
                    duplicate_run.error() == qa::TakeRunSequenceError::duplicate_run,
                "ambiguous_take_sequence_was_accepted");

        std::puts("take_run_sequence_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "take_run_sequence_test: %s\n", error.what());
        return 1;
    }
}
