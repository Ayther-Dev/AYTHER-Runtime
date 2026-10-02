#include "coverage_model.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {

void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

qa::StageObservation complete(std::uint64_t count, std::uint64_t fact_sequence) {
    return {qa::ObservationExtent::complete,
            {qa::Availability::known, count, {}},
            {qa::Availability::known, qa::FactId{"run", "coverage", fact_sequence}, {}}};
}

qa::Coverage base_coverage() {
    qa::Coverage coverage;
    coverage.run_id = "run";
    coverage.assignment_id = {"pack", 3};
    coverage.load = complete(1, 1);
    coverage.preexisting_occurrences = complete(7, 7);
    return coverage;
}

void verify_selection_without_start() {
    auto coverage = base_coverage();
    coverage.candidates = complete(4, 2);
    coverage.selections = complete(1, 3);
    coverage.playback_starts = complete(0, 4);
    coverage.playback_advances = complete(0, 5);
    coverage.playback_ends = complete(0, 6);

    const auto summary = qa::summarize_assignment_stages(coverage);
    require(summary.has_value(), "valid_stages_rejected");
    require(summary->candidates == qa::StageCoverage::observed &&
                summary->selections == qa::StageCoverage::observed,
            "candidate_or_selection_lost");
    require(summary->playback_starts == qa::StageCoverage::observed_zero &&
                summary->playback_advances == qa::StageCoverage::observed_zero &&
                summary->playback_ends == qa::StageCoverage::observed_zero,
            "selection_implied_playback_lifecycle");
}

void verify_full_lifecycle() {
    auto coverage = base_coverage();
    coverage.candidates = complete(2, 2);
    coverage.selections = complete(1, 3);
    coverage.playback_starts = complete(1, 4);
    coverage.playback_advances = complete(12, 5);
    coverage.playback_ends = complete(1, 6);

    const auto summary = qa::summarize_assignment_stages(coverage);
    require(summary && summary->playback_starts == qa::StageCoverage::observed &&
                summary->playback_advances == qa::StageCoverage::observed &&
                summary->playback_ends == qa::StageCoverage::observed,
            "playback_lifecycle_stages_collapsed");
}

void verify_partial_zero_is_unknown() {
    auto coverage = base_coverage();
    coverage.candidates = complete(1, 2);
    coverage.selections = complete(1, 3);
    coverage.playback_starts = {qa::ObservationExtent::partial,
                                {qa::Availability::known, 0, {}},
                                {qa::Availability::known, qa::FactId{"run", "coverage", 4}, {}}};
    const auto summary = qa::summarize_assignment_stages(coverage);
    require(summary && summary->playback_starts == qa::StageCoverage::unknown,
            "partial_zero_presented_as_observed_absence");

    coverage.playback_starts.observation_fact.value->run_id = "other-run";
    require(!qa::summarize_assignment_stages(coverage), "foreign_stage_fact_accepted");
}

} // namespace

int main() {
    try {
        verify_selection_without_start();
        verify_full_lifecycle();
        verify_partial_zero_is_unknown();
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "qa_assignment_stage_coverage_failed: %s\n", error.what());
        return 1;
    }
}
