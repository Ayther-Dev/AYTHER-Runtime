#include "coverage_model.h"

#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

namespace qa = ayther::audio_qa;
namespace {

void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

qa::StageObservation complete(std::string run_id, std::uint64_t count, std::uint64_t sequence) {
    return {qa::ObservationExtent::complete,
            {qa::Availability::known, count, {}},
            {qa::Availability::known, qa::FactId{std::move(run_id), "coverage", sequence}, {}}};
}

qa::Coverage row(std::string run_id, std::uint64_t ordinal, std::uint64_t candidates,
                 std::uint64_t starts) {
    qa::Coverage result;
    result.run_id = std::move(run_id);
    result.assignment_id = {"pack", ordinal};
    result.load = complete(result.run_id, 1, 1);
    result.candidates = complete(result.run_id, candidates, 2);
    result.selections = complete(result.run_id, starts, 3);
    result.playback_starts = complete(result.run_id, starts, 4);
    result.playback_advances = complete(result.run_id, starts * 4, 5);
    result.playback_ends = complete(result.run_id, starts, 6);
    result.preexisting_occurrences = complete(result.run_id, 0, 7);
    return result;
}

void verify_multi_run_provenance() {
    const std::array rows{row("run-main", 2, 1, 0), row("run-demo", 2, 1, 1),
                          row("run-demo", 5, 1, 1)};
    const auto result = qa::aggregate_assignment_coverage(rows);
    require(result.status == qa::CoverageAggregationStatus::aggregated &&
                result.assignments.size() == 2,
            "repeated_assignment_counted_twice");
    const auto &repeated = result.assignments[0];
    require(repeated.assignment_id == rows[0].assignment_id && repeated.candidates.size() == 2 &&
                repeated.playback_starts.size() == 2,
            "repeated_assignment_stages_not_aggregated");
    require(repeated.candidates[0].run_id == "run-main" &&
                repeated.candidates[1].run_id == "run-demo" &&
                repeated.playback_starts[0].run_id == "run-main" &&
                repeated.playback_starts[1].run_id == "run-demo",
            "stage_provenance_lost");
    require(repeated.playback_starts[0].observation.count.value == 0 &&
                repeated.playback_starts[1].observation.count.value == 1,
            "per_run_stage_counts_collapsed");
}

void verify_duplicate_row_rejected() {
    const auto first = row("run", 1, 1, 0);
    const std::array rows{first, first};
    const auto result = qa::aggregate_assignment_coverage(rows);
    require(result.status == qa::CoverageAggregationStatus::duplicate_run_assignment &&
                result.assignments.empty(),
            "duplicate_run_assignment_accepted");
}

} // namespace

int main() {
    try {
        verify_multi_run_provenance();
        verify_duplicate_row_rejected();
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "qa_assignment_coverage_aggregation_failed: %s\n", error.what());
        return 1;
    }
}
