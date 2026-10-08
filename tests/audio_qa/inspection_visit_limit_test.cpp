// Spec 002, plan §8 P-14 (RNF-3, RF-2.13): a traversal keeps at most 100 000
// inspection visits. The exact boundary is complete; the next visit is diagnosed as
// incomplete(limit), is not retained, and cannot change the last confirmed visit.
#include "evidence_completeness.h"
#include "inspection_evidence.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

qa::InspectionEvent pause(const std::uint64_t sequence) {
    return {sequence, "pause", sequence % 60U, sequence % 60U, sequence, sequence * 10U};
}

qa::EvidenceParts complete_parts() {
    qa::EvidenceParts parts;
    parts.identification = true;
    parts.conditions = true;
    parts.positions = true;
    parts.controls = true;
    parts.result = true;
    parts.data_without_losses = true;
    parts.fragments_flushed = true;
    return parts;
}

qa::TraversalRecorder recorder_with(const std::uint64_t visits) {
    qa::TraversalRecorder recorder{60U};
    for (std::uint64_t sequence = 1U; sequence <= visits; ++sequence)
        recorder.inspection(pause(sequence));
    return recorder;
}

} // namespace

int main() {
    auto at_limit = recorder_with(100'000U);
    const auto at_limit_document = at_limit.document();
    auto at_limit_parts = complete_parts();
    at_limit_parts.within_limits = !at_limit.visit_limit_exceeded();
    const auto at_limit_evidence = qa::evaluate_evidence(at_limit_parts);
    expect(at_limit_document.visits.size() == 100'000U &&
               at_limit_document.visits.back().seq == 100'000U && !at_limit.visit_limit_exceeded(),
           "P-14, RNF-3: exactly 100000 inspection visits are retained");
    expect(at_limit_evidence.complete && at_limit_evidence.reasons.empty(),
           "P-14, RF-2.13: evidence at the visit limit remains complete");

    auto above_limit = recorder_with(100'001U);
    const auto above_limit_document = above_limit.document();
    auto above_limit_parts = complete_parts();
    above_limit_parts.within_limits = !above_limit.visit_limit_exceeded();
    const auto above_limit_evidence = qa::evaluate_evidence(above_limit_parts);
    expect(above_limit_document.visits.size() == 100'000U &&
               above_limit_document.visits.back().seq == 100'000U,
           "P-14, RNF-3: visit 100001 is diagnosed without unbounded retention");
    expect(above_limit.visit_limit_exceeded() && !above_limit_evidence.complete &&
               above_limit_evidence.reasons == std::vector<std::string>{"limit"},
           "P-14, RNF-3: visit 100001 makes evidence incomplete(limit)");

    above_limit.inspection(pause(100'002U));
    above_limit.frame_played(7U);
    const auto after_more_input = above_limit.document();
    expect(after_more_input.visits.size() == 100'000U &&
               after_more_input.visits.back().seq == 100'000U &&
               after_more_input.resume_after.size() == 1U &&
               after_more_input.resume_after.back().seq == 100'000U,
           "P-14, RNF-5: excess visits cannot corrupt the last retained result");

    std::vector<qa::InspectionEvent> events;
    for (std::uint64_t sequence = 1U; sequence <= 100'000U; ++sequence)
        events.push_back(pause(sequence));
    events.push_back({100'001U, "interrupted", 5U, 5U, 100'001U, 1'000'010U});
    expect(!qa::exceeds_visit_limit(events),
           "P-14: 100000 visits and an interruption stay within the limit");
    events.push_back(pause(100'002U));
    expect(qa::exceeds_visit_limit(events),
           "P-14, RNF-3: the run reports visit 100001 so its summary says incomplete(limit)");

    if (failures != 0)
        return 1;
    std::cout << "inspection visit limit: 100000 complete, 100001 incomplete(limit)\n";
    return 0;
}
