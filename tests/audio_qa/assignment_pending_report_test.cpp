#include "coverage_model.h"

#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace qa = ayther::audio_qa;
namespace {

void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

qa::Assignment assignment(std::uint64_t ordinal) {
    qa::Assignment result;
    result.id = {"pack", ordinal};
    result.authored_id = {qa::Availability::known, "assignment-" + std::to_string(ordinal), {}};
    result.load_state = qa::LoadState::loaded;
    return result;
}

qa::StageObservation complete(std::uint64_t count, std::uint64_t sequence) {
    return {qa::ObservationExtent::complete,
            {qa::Availability::known, count, {}},
            {qa::Availability::known, qa::FactId{"run", "coverage", sequence}, {}}};
}

qa::Coverage coverage(std::uint64_t ordinal, std::uint64_t candidates) {
    qa::Coverage result;
    result.run_id = "run";
    result.assignment_id = {"pack", ordinal};
    result.load = complete(1, ordinal * 10 + 1);
    result.candidates = complete(candidates, ordinal * 10 + 2);
    result.selections = complete(0, ordinal * 10 + 3);
    result.playback_starts = complete(0, ordinal * 10 + 4);
    result.playback_advances = complete(0, ordinal * 10 + 5);
    result.playback_ends = complete(0, ordinal * 10 + 6);
    result.preexisting_occurrences = complete(0, ordinal * 10 + 7);
    return result;
}

void verify_partial_coverage_keeps_complete_evidence() {
    const std::array inventory{assignment(0), assignment(1), assignment(2)};
    const std::array rows{coverage(0, 1), coverage(1, 0)};
    const qa::AssignmentCoverageReportRequest request{"run", qa::ObservationExtent::complete,
                                                      qa::EvidenceResult::complete};
    const auto report = qa::summarize_assignment_coverage(request, inventory, rows);
    require(report.status == qa::AssignmentCoverageReportStatus::summarized &&
                report.detected_count.value == 1 && report.observed_detected_count == 1 &&
                report.partial_coverage,
            "partial_coverage_not_reported");
    require(report.pending_assignments.size() == 2 &&
                report.pending_assignments[0] == inventory[1].id &&
                report.pending_assignments[1] == inventory[2].id,
            "pending_assignments_not_listed");
    require(report.evidence_result == qa::EvidenceResult::complete,
            "partial_coverage_made_evidence_incomplete");
}

void verify_absent_source_is_not_zero() {
    const std::array inventory{assignment(0), assignment(1)};
    const qa::AssignmentCoverageReportRequest request{"run", qa::ObservationExtent::not_observed,
                                                      qa::EvidenceResult::complete};
    const auto report =
        qa::summarize_assignment_coverage(request, inventory, std::span<const qa::Coverage>{});
    require(report.status == qa::AssignmentCoverageReportStatus::summarized &&
                !report.detected_count.value &&
                report.detected_count.reason_code == "detection_source_not_observed" &&
                report.observed_detected_count == 0,
            "absent_source_presented_as_observed_zero");
    require(report.pending_assignments.size() == inventory.size() &&
                report.evidence_result == qa::EvidenceResult::complete,
            "absent_source_lost_pending_inventory_or_changed_evidence");
}

void verify_complete_source_can_report_zero() {
    const std::array inventory{assignment(0), assignment(1)};
    const qa::AssignmentCoverageReportRequest request{"run", qa::ObservationExtent::complete,
                                                      qa::EvidenceResult::complete};
    const auto report =
        qa::summarize_assignment_coverage(request, inventory, std::span<const qa::Coverage>{});
    require(report.detected_count.value == 0 &&
                report.pending_assignments.size() == inventory.size(),
            "verified_zero_not_reported");
}

} // namespace

int main() {
    try {
        verify_partial_coverage_keeps_complete_evidence();
        verify_absent_source_is_not_zero();
        verify_complete_source_can_report_zero();
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "qa_assignment_pending_report_failed: %s\n", error.what());
        return 1;
    }
}
