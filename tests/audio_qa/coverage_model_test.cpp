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
qa::StageObservation observed(std::uint64_t count) {
    return {qa::ObservationExtent::complete,
            {qa::Availability::known, count, {}},
            {qa::Availability::known, qa::FactId{"run", "coverage", 1}, {}}};
}
} // namespace

int main() {
    try {
        qa::Assignment first;
        first.id = {"pack-content", 0};
        first.authored_id = {qa::Availability::known, "same-authored-id", {}};
        first.load_state = qa::LoadState::loaded;
        auto duplicate = first;
        duplicate.id.ordinal = 1;
        require(qa::well_formed(first) && qa::well_formed(duplicate) && first.id != duplicate.id,
                "duplicate_authored_id_merged_entries");
        duplicate.load_state = qa::LoadState::rejected;
        require(!qa::well_formed(duplicate), "rejection_without_reason_accepted");
        duplicate.load_reason = {qa::Availability::known, "duplicate_authored_id", {}};
        require(qa::well_formed(duplicate), "rejection_diagnostic_unrepresentable");
        qa::Coverage coverage;
        coverage.run_id = "run";
        coverage.assignment_id = first.id;
        coverage.load = observed(1);
        coverage.candidates = observed(5);
        coverage.preexisting_occurrences = observed(1);
        require(qa::selection_coverage(coverage) == qa::SelectionCoverage::unknown,
                "load_candidate_or_preexisting_counted_as_selection");
        coverage.selections = observed(0);
        require(qa::selection_coverage(coverage) == qa::SelectionCoverage::pending,
                "observed_zero_selection_not_pending");
        coverage.selections.observation_fact = {};
        require(!qa::well_formed(coverage), "unverified_zero_counted_as_complete");
        coverage.selections = observed(1);
        coverage.playback_starts = observed(0);
        require(qa::selection_coverage(coverage) == qa::SelectionCoverage::selected &&
                    coverage.playback_starts.count.value == 0,
                "selection_implied_playback");
        coverage.selections.extent = qa::ObservationExtent::partial;
        require(qa::selection_coverage(coverage) == qa::SelectionCoverage::selected,
                "observed_selection_lost_in_partial_trace");
        coverage.selections.count.value = 0;
        require(qa::selection_coverage(coverage) == qa::SelectionCoverage::unknown,
                "partial_zero_counted_as_observed_absence");
        qa::Diagnostic diagnostic;
        diagnostic.code = "trace_gap";
        diagnostic.subject_id = "run";
        diagnostic.stage = "capture";
        diagnostic.lost_fact_count = {qa::Availability::unknown, std::nullopt, "unknown_gap_size"};
        require(qa::well_formed(diagnostic), "unknown_loss_diagnostic_rejected");
        diagnostic.detail.assign(qa::max_reference_value_bytes, 'x');
        require(qa::well_formed(diagnostic), "diagnostic_limit_boundary_rejected");
        diagnostic.detail.push_back('x');
        require(!qa::well_formed(diagnostic), "diagnostic_limit_ignored");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "qa_coverage_test_failed: %s\n", error.what());
        return 1;
    }
}
