#include "coverage_model.h"

#include <algorithm>
#include <iterator>
#include <string_view>
#include <utility>

namespace ayther::audio_qa {
namespace {
bool identifier(std::string_view value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}
bool valid(const Field<std::string> &field) noexcept {
    return consistent_availability(field) && (!field.value || identifier(*field.value));
}
bool valid(const Field<FactId> &field) noexcept {
    return consistent_availability(field) &&
           (!field.value ||
            (identifier(field.value->run_id) && identifier(field.value->producer_id) &&
             field.value->producer_sequence != 0));
}
bool valid(const StageObservation &stage, std::string_view run) noexcept {
    if (!consistent_availability(stage.count) || !valid(stage.observation_fact) ||
        (stage.observation_fact.value && stage.observation_fact.value->run_id != run)) {
        return false;
    }
    switch (stage.extent) {
    case ObservationExtent::not_observed:
        return !stage.count.value;
    case ObservationExtent::partial:
        return true;
    case ObservationExtent::complete:
        return stage.count.value.has_value() && stage.observation_fact.value.has_value();
    }
    return false;
}

StageCoverage classify(const StageObservation &stage) noexcept {
    if (stage.count.value && *stage.count.value > 0) {
        return StageCoverage::observed;
    }
    if (stage.extent == ObservationExtent::complete && stage.count.value) {
        return StageCoverage::observed_zero;
    }
    return StageCoverage::unknown;
}

template <class T> Field<T> known(T value) { return {Availability::known, std::move(value), {}}; }

template <class T> Field<T> unknown(std::string reason) {
    return {Availability::unknown, std::nullopt, std::move(reason)};
}

void make_load_counts_unknown(AssignmentInventorySummary &summary, std::string reason) {
    summary.unknown_load_count = unknown<std::uint64_t>(reason);
    summary.pending_load_count = unknown<std::uint64_t>(reason);
    summary.loaded_count = unknown<std::uint64_t>(reason);
    summary.rejected_count = unknown<std::uint64_t>(std::move(reason));
}
} // namespace

bool well_formed(const Assignment &assignment) noexcept {
    return identifier(assignment.id.pack_content_id) && valid(assignment.authored_id) &&
           consistent_availability(assignment.expected) && valid(assignment.load_reason) &&
           valid(assignment.load_fact) && assignment.load_state >= LoadState::unknown &&
           assignment.load_state <= LoadState::rejected &&
           (assignment.load_state != LoadState::rejected ||
            assignment.load_reason.value.has_value());
}

bool well_formed(const Coverage &coverage) noexcept {
    return identifier(coverage.run_id) && identifier(coverage.assignment_id.pack_content_id) &&
           valid(coverage.load, coverage.run_id) && valid(coverage.candidates, coverage.run_id) &&
           valid(coverage.selections, coverage.run_id) &&
           valid(coverage.playback_starts, coverage.run_id) &&
           valid(coverage.playback_advances, coverage.run_id) &&
           valid(coverage.playback_ends, coverage.run_id) &&
           valid(coverage.preexisting_occurrences, coverage.run_id);
}

SelectionCoverage selection_coverage(const Coverage &coverage) noexcept {
    if (!well_formed(coverage)) {
        return SelectionCoverage::unknown;
    }
    const auto &stage = coverage.selections;
    if (stage.count.value && *stage.count.value > 0) {
        return SelectionCoverage::selected;
    }
    return stage.extent == ObservationExtent::complete ? SelectionCoverage::pending
                                                       : SelectionCoverage::unknown;
}

std::optional<AssignmentStageSummary>
summarize_assignment_stages(const Coverage &coverage) noexcept {
    if (!well_formed(coverage)) {
        return std::nullopt;
    }
    return AssignmentStageSummary{coverage.assignment_id,
                                  classify(coverage.candidates),
                                  classify(coverage.selections),
                                  classify(coverage.playback_starts),
                                  classify(coverage.playback_advances),
                                  classify(coverage.playback_ends)};
}

AssignmentCoverageReport
summarize_assignment_coverage(const AssignmentCoverageReportRequest &request,
                              std::span<const Assignment> inventory,
                              std::span<const Coverage> coverage_rows) {
    AssignmentCoverageReport report;
    report.run_id = request.run_id;
    report.detection_source_extent = request.detection_source_extent;
    report.evidence_result = request.evidence_result;
    const bool extent_valid = request.detection_source_extent >= ObservationExtent::not_observed &&
                              request.detection_source_extent <= ObservationExtent::complete;
    const bool evidence_valid = request.evidence_result >= EvidenceResult::pending &&
                                request.evidence_result <= EvidenceResult::incomplete;
    if (!identifier(request.run_id) || !extent_valid || !evidence_valid ||
        (request.detection_source_extent == ObservationExtent::not_observed &&
         !coverage_rows.empty())) {
        return report;
    }
    if (inventory.size() > max_reference_materials ||
        coverage_rows.size() > max_reference_materials) {
        report.status = AssignmentCoverageReportStatus::capacity_exceeded;
        return report;
    }

    std::vector<AssignmentId> assignment_ids;
    assignment_ids.reserve(inventory.size());
    for (const auto &assignment : inventory) {
        if (!well_formed(assignment)) {
            report.status = AssignmentCoverageReportStatus::invalid_inventory;
            return report;
        }
        const auto duplicate =
            std::find(assignment_ids.begin(), assignment_ids.end(), assignment.id);
        if (duplicate == assignment_ids.end()) {
            assignment_ids.push_back(assignment.id);
        }
    }

    std::vector<AssignmentId> covered_ids;
    std::vector<AssignmentId> coverage_ids;
    covered_ids.reserve(coverage_rows.size());
    coverage_ids.reserve(coverage_rows.size());
    for (const auto &coverage : coverage_rows) {
        if (!well_formed(coverage) || coverage.run_id != request.run_id ||
            std::find(assignment_ids.begin(), assignment_ids.end(), coverage.assignment_id) ==
                assignment_ids.end()) {
            report.status = AssignmentCoverageReportStatus::invalid_coverage;
            return report;
        }
        const auto duplicate =
            std::find(coverage_ids.begin(), coverage_ids.end(), coverage.assignment_id);
        if (duplicate != coverage_ids.end()) {
            report.status = AssignmentCoverageReportStatus::duplicate_coverage;
            return report;
        }
        coverage_ids.push_back(coverage.assignment_id);
        const auto stages = summarize_assignment_stages(coverage);
        if (!stages) {
            report.status = AssignmentCoverageReportStatus::invalid_coverage;
            return report;
        }
        const bool detected = stages->candidates == StageCoverage::observed ||
                              stages->selections == StageCoverage::observed ||
                              stages->playback_starts == StageCoverage::observed ||
                              stages->playback_advances == StageCoverage::observed ||
                              stages->playback_ends == StageCoverage::observed;
        if (detected) {
            covered_ids.push_back(coverage.assignment_id);
        }
    }

    report.observed_detected_count = covered_ids.size();
    if (request.detection_source_extent == ObservationExtent::complete) {
        report.detected_count = known(report.observed_detected_count);
    } else {
        report.detected_count =
            unknown<std::uint64_t>(request.detection_source_extent == ObservationExtent::partial
                                       ? "partial_detection_source"
                                       : "detection_source_not_observed");
    }
    for (const auto &assignment_id : assignment_ids) {
        if (std::find(covered_ids.begin(), covered_ids.end(), assignment_id) == covered_ids.end()) {
            report.pending_assignments.push_back(assignment_id);
        }
    }
    report.partial_coverage = !report.pending_assignments.empty();
    report.status = AssignmentCoverageReportStatus::summarized;
    return report;
}

CoverageAggregation aggregate_assignment_coverage(std::span<const Coverage> coverage_rows) {
    CoverageAggregation result;
    if (coverage_rows.size() > max_reference_materials) {
        result.status = CoverageAggregationStatus::capacity_exceeded;
        return result;
    }
    result.assignments.reserve(coverage_rows.size());
    std::vector<std::pair<std::string, AssignmentId>> seen_rows;
    seen_rows.reserve(coverage_rows.size());
    for (const auto &coverage : coverage_rows) {
        if (!well_formed(coverage)) {
            result.assignments.clear();
            return result;
        }
        const auto row = std::pair{coverage.run_id, coverage.assignment_id};
        if (std::find(seen_rows.begin(), seen_rows.end(), row) != seen_rows.end()) {
            result.status = CoverageAggregationStatus::duplicate_run_assignment;
            result.assignments.clear();
            return result;
        }
        seen_rows.push_back(row);

        auto assignment = std::find_if(result.assignments.begin(), result.assignments.end(),
                                       [&](const AggregatedAssignmentCoverage &candidate) {
                                           return candidate.assignment_id == coverage.assignment_id;
                                       });
        if (assignment == result.assignments.end()) {
            AggregatedAssignmentCoverage aggregated;
            aggregated.assignment_id = coverage.assignment_id;
            result.assignments.push_back(std::move(aggregated));
            assignment = std::prev(result.assignments.end());
        }
        const auto append = [&](std::vector<ProvenancedStageObservation> &target,
                                const StageObservation &observation) {
            target.push_back({coverage.run_id, observation});
        };
        append(assignment->load, coverage.load);
        append(assignment->candidates, coverage.candidates);
        append(assignment->selections, coverage.selections);
        append(assignment->playback_starts, coverage.playback_starts);
        append(assignment->playback_advances, coverage.playback_advances);
        append(assignment->playback_ends, coverage.playback_ends);
        append(assignment->preexisting_occurrences, coverage.preexisting_occurrences);
    }
    result.status = CoverageAggregationStatus::aggregated;
    return result;
}

AssignmentInventorySummary
summarize_assignment_inventory(const AssignmentInventoryReference &reference,
                               std::span<const Assignment> observations) {
    AssignmentInventorySummary summary;
    summary.pack_content_id = reference.pack_content_id;
    summary.expected_count_source = reference.expected_count_source;
    summary.inventory_extent = reference.inventory_extent;

    const bool fixed =
        reference.expected_count_source == ExpectedAssignmentCountSource::fixed_reference;
    const bool source_valid =
        reference.expected_count_source >= ExpectedAssignmentCountSource::fixed_reference &&
        reference.expected_count_source <= ExpectedAssignmentCountSource::observed_inventory;
    const bool extent_valid = reference.inventory_extent >= ObservationExtent::not_observed &&
                              reference.inventory_extent <= ObservationExtent::complete;
    if (!identifier(reference.pack_content_id) || !source_valid || !extent_valid ||
        fixed != reference.fixed_expected_count.has_value() ||
        (reference.fixed_expected_count &&
         *reference.fixed_expected_count > max_reference_materials) ||
        (reference.inventory_extent == ObservationExtent::not_observed && !observations.empty())) {
        return summary;
    }
    if (observations.size() > max_reference_materials) {
        summary.status = AssignmentInventoryStatus::capacity_exceeded;
        return summary;
    }

    std::vector<Assignment> assignments;
    assignments.reserve(observations.size());
    for (const auto &assignment : observations) {
        if (!well_formed(assignment)) {
            summary.status = AssignmentInventoryStatus::invalid_assignment;
            summary.first_problem = known(assignment.id);
            return summary;
        }
        if (assignment.id.pack_content_id != reference.pack_content_id) {
            summary.status = AssignmentInventoryStatus::pack_mismatch;
            summary.first_problem = known(assignment.id);
            return summary;
        }
        const auto duplicate =
            std::find_if(assignments.begin(), assignments.end(), [&](const Assignment &candidate) {
                return candidate.id == assignment.id;
            });
        if (duplicate == assignments.end()) {
            assignments.push_back(assignment);
            continue;
        }
        ++summary.duplicate_observation_count;
        if (*duplicate != assignment &&
            summary.status != AssignmentInventoryStatus::conflicting_duplicate) {
            summary.status = AssignmentInventoryStatus::conflicting_duplicate;
            summary.first_problem = known(assignment.id);
        }
    }

    summary.observed_assignment_count = assignments.size();
    if (reference.inventory_extent == ObservationExtent::complete) {
        summary.inventory_count = known(summary.observed_assignment_count);
    } else {
        summary.inventory_count = unknown<std::uint64_t>(
            reference.inventory_extent == ObservationExtent::partial ? "partial_inventory"
                                                                     : "inventory_not_observed");
    }
    if (fixed) {
        summary.expected_count = known(*reference.fixed_expected_count);
    } else if (summary.inventory_count.value) {
        summary.expected_count = known(*summary.inventory_count.value);
    } else {
        summary.expected_count = unknown<std::uint64_t>(summary.inventory_count.reason_code);
    }

    if (summary.status == AssignmentInventoryStatus::conflicting_duplicate) {
        make_load_counts_unknown(summary, "conflicting_duplicate");
    } else {
        std::uint64_t unknown_count = 0;
        std::uint64_t pending_count = 0;
        std::uint64_t loaded_count = 0;
        std::uint64_t rejected_count = 0;
        for (const auto &assignment : assignments) {
            switch (assignment.load_state) {
            case LoadState::unknown:
                ++unknown_count;
                break;
            case LoadState::pending:
                ++pending_count;
                break;
            case LoadState::loaded:
                ++loaded_count;
                break;
            case LoadState::rejected:
                ++rejected_count;
                break;
            }
        }
        summary.unknown_load_count = known(unknown_count);
        summary.pending_load_count = known(pending_count);
        summary.loaded_count = known(loaded_count);
        summary.rejected_count = known(rejected_count);
        summary.status = AssignmentInventoryStatus::summarized;
    }

    if (summary.inventory_count.value && summary.expected_count.value) {
        summary.inventory_matches_expectation =
            known(*summary.inventory_count.value == *summary.expected_count.value);
    } else {
        summary.inventory_matches_expectation = unknown<bool>("inventory_or_expectation_unknown");
    }
    return summary;
}

bool well_formed(const Diagnostic &diagnostic) noexcept {
    return identifier(diagnostic.code) && identifier(diagnostic.subject_id) &&
           identifier(diagnostic.stage) && diagnostic.scope >= DiagnosticScope::request &&
           diagnostic.scope <= DiagnosticScope::compatibility &&
           diagnostic.severity >= DiagnosticSeverity::info &&
           diagnostic.severity <= DiagnosticSeverity::error && valid(diagnostic.source_fact) &&
           consistent_availability(diagnostic.affected_samples) &&
           (!diagnostic.affected_samples.value ||
            well_formed(*diagnostic.affected_samples.value)) &&
           consistent_availability(diagnostic.last_confirmed_frame) &&
           consistent_availability(diagnostic.lost_fact_count) &&
           diagnostic.detail.size() <= max_reference_value_bytes;
}

} // namespace ayther::audio_qa
