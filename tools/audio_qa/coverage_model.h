#pragma once

#include "mix_model.h"
#include "model.h"

#include <span>

namespace ayther::audio_qa {

struct AssignmentId {
    std::string pack_content_id;
    std::uint64_t ordinal{};
    bool operator==(const AssignmentId &) const = default;
};
enum class LoadState { unknown, pending, loaded, rejected };
struct Assignment {
    AssignmentId id;
    Field<std::string> authored_id;
    Field<bool> expected;
    LoadState load_state{LoadState::unknown};
    Field<std::string> load_reason;
    Field<FactId> load_fact;
    bool operator==(const Assignment &) const = default;
};

enum class ObservationExtent { not_observed, partial, complete };
struct StageObservation {
    ObservationExtent extent{ObservationExtent::not_observed};
    Field<std::uint64_t> count;
    Field<FactId> observation_fact;
};
struct Coverage {
    std::string run_id;
    AssignmentId assignment_id;
    StageObservation load;
    StageObservation candidates;
    StageObservation selections;
    StageObservation playback_starts;
    StageObservation playback_advances;
    StageObservation playback_ends;
    StageObservation preexisting_occurrences;
};
enum class SelectionCoverage { unknown, pending, selected };
enum class StageCoverage { unknown, observed_zero, observed };
struct AssignmentStageSummary {
    AssignmentId assignment_id;
    StageCoverage candidates{StageCoverage::unknown};
    StageCoverage selections{StageCoverage::unknown};
    StageCoverage playback_starts{StageCoverage::unknown};
    StageCoverage playback_advances{StageCoverage::unknown};
    StageCoverage playback_ends{StageCoverage::unknown};
};
struct AssignmentCoverageReportRequest {
    std::string run_id;
    ObservationExtent detection_source_extent{ObservationExtent::not_observed};
    EvidenceResult evidence_result{EvidenceResult::pending};
};
enum class AssignmentCoverageReportStatus {
    summarized,
    invalid_request,
    invalid_inventory,
    invalid_coverage,
    duplicate_coverage,
    capacity_exceeded
};
struct AssignmentCoverageReport {
    AssignmentCoverageReportStatus status{AssignmentCoverageReportStatus::invalid_request};
    std::string run_id;
    ObservationExtent detection_source_extent{ObservationExtent::not_observed};
    EvidenceResult evidence_result{EvidenceResult::pending};
    std::uint64_t observed_detected_count{};
    Field<std::uint64_t> detected_count;
    std::vector<AssignmentId> pending_assignments;
    bool partial_coverage{};
};
struct ProvenancedStageObservation {
    std::string run_id;
    StageObservation observation;
};
struct AggregatedAssignmentCoverage {
    AssignmentId assignment_id;
    std::vector<ProvenancedStageObservation> load;
    std::vector<ProvenancedStageObservation> candidates;
    std::vector<ProvenancedStageObservation> selections;
    std::vector<ProvenancedStageObservation> playback_starts;
    std::vector<ProvenancedStageObservation> playback_advances;
    std::vector<ProvenancedStageObservation> playback_ends;
    std::vector<ProvenancedStageObservation> preexisting_occurrences;
};
enum class CoverageAggregationStatus {
    aggregated,
    invalid_coverage,
    duplicate_run_assignment,
    capacity_exceeded
};
struct CoverageAggregation {
    CoverageAggregationStatus status{CoverageAggregationStatus::invalid_coverage};
    std::vector<AggregatedAssignmentCoverage> assignments;
};

enum class ExpectedAssignmentCountSource { fixed_reference, observed_inventory };
struct AssignmentInventoryReference {
    std::string pack_content_id;
    ExpectedAssignmentCountSource expected_count_source{
        ExpectedAssignmentCountSource::observed_inventory};
    std::optional<std::uint64_t> fixed_expected_count;
    ObservationExtent inventory_extent{ObservationExtent::not_observed};
};
enum class AssignmentInventoryStatus {
    summarized,
    invalid_reference,
    invalid_assignment,
    pack_mismatch,
    capacity_exceeded,
    conflicting_duplicate
};
struct AssignmentInventorySummary {
    AssignmentInventoryStatus status{AssignmentInventoryStatus::invalid_reference};
    std::string pack_content_id;
    ExpectedAssignmentCountSource expected_count_source{
        ExpectedAssignmentCountSource::observed_inventory};
    ObservationExtent inventory_extent{ObservationExtent::not_observed};
    std::uint64_t observed_assignment_count{};
    std::uint64_t duplicate_observation_count{};
    Field<std::uint64_t> inventory_count;
    Field<std::uint64_t> expected_count;
    Field<std::uint64_t> unknown_load_count;
    Field<std::uint64_t> pending_load_count;
    Field<std::uint64_t> loaded_count;
    Field<std::uint64_t> rejected_count;
    Field<bool> inventory_matches_expectation;
    Field<AssignmentId> first_problem;
};

enum class DiagnosticScope { request, run, material, trace, audio, storage, compatibility };
enum class DiagnosticSeverity { info, warning, error };
struct Diagnostic {
    std::string code;
    DiagnosticScope scope{DiagnosticScope::run};
    DiagnosticSeverity severity{DiagnosticSeverity::error};
    std::string subject_id;
    std::string stage;
    Field<FactId> source_fact;
    Field<SampleFrameRange> affected_samples;
    Field<std::uint64_t> last_confirmed_frame;
    Field<std::uint64_t> lost_fact_count;
    std::string detail;
    bool operator==(const Diagnostic &) const = default;
};

[[nodiscard]] bool well_formed(const Assignment &assignment) noexcept;
[[nodiscard]] bool well_formed(const Coverage &coverage) noexcept;
[[nodiscard]] bool well_formed(const Diagnostic &diagnostic) noexcept;
// Technical coverage only. No musical verdict is represented by these types.
[[nodiscard]] SelectionCoverage selection_coverage(const Coverage &coverage) noexcept;
[[nodiscard]] std::optional<AssignmentStageSummary>
summarize_assignment_stages(const Coverage &coverage) noexcept;
[[nodiscard]] AssignmentCoverageReport
summarize_assignment_coverage(const AssignmentCoverageReportRequest &request,
                              std::span<const Assignment> inventory,
                              std::span<const Coverage> coverage_rows);
[[nodiscard]] CoverageAggregation
aggregate_assignment_coverage(std::span<const Coverage> coverage_rows);
[[nodiscard]] AssignmentInventorySummary
summarize_assignment_inventory(const AssignmentInventoryReference &reference,
                               std::span<const Assignment> observations);

} // namespace ayther::audio_qa
