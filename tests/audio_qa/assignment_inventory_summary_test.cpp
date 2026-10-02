#include "coverage_model.h"

#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

qa::Assignment assignment(std::string pack, std::uint64_t ordinal, qa::LoadState state,
                          std::string authored) {
    qa::Assignment result;
    result.id = {std::move(pack), ordinal};
    result.authored_id = {qa::Availability::known, std::move(authored), {}};
    result.load_state = state;
    if (state == qa::LoadState::rejected) {
        result.load_reason = {qa::Availability::known, "missing_asset", {}};
    }
    return result;
}

void verify_initial_reference() {
    std::vector<qa::Assignment> observations;
    for (std::uint64_t ordinal = 0; ordinal < 14; ++ordinal) {
        const auto state = ordinal < 10    ? qa::LoadState::loaded
                           : ordinal < 12  ? qa::LoadState::rejected
                           : ordinal == 12 ? qa::LoadState::pending
                                           : qa::LoadState::unknown;
        observations.push_back(
            assignment("golden-axe-content", ordinal, state,
                       ordinal < 2 ? "same-authored-id" : "assignment-" + std::to_string(ordinal)));
    }
    observations.push_back(observations.front());

    const qa::AssignmentInventoryReference reference{
        "golden-axe-content", qa::ExpectedAssignmentCountSource::fixed_reference, 14,
        qa::ObservationExtent::complete};
    const auto summary = qa::summarize_assignment_inventory(reference, observations);
    require(summary.status == qa::AssignmentInventoryStatus::summarized,
            "initial_reference_not_summarized");
    require(summary.observed_assignment_count == 14 && summary.duplicate_observation_count == 1,
            "repeated_observation_inflated_inventory");
    require(summary.inventory_count.value == 14 && summary.expected_count.value == 14 &&
                summary.inventory_matches_expectation.value == true,
            "fixed_expectation_not_compared");
    require(summary.loaded_count.value == 10 && summary.rejected_count.value == 2 &&
                summary.pending_load_count.value == 1 && summary.unknown_load_count.value == 1,
            "load_states_not_separated");
}

void verify_other_reference() {
    std::vector<qa::Assignment> observations{
        assignment("other-pack", 0, qa::LoadState::loaded, "same"),
        assignment("other-pack", 1, qa::LoadState::loaded, "same"),
        assignment("other-pack", 2, qa::LoadState::pending, "third")};
    observations.push_back(observations[1]);
    const qa::AssignmentInventoryReference reference{
        "other-pack", qa::ExpectedAssignmentCountSource::observed_inventory, std::nullopt,
        qa::ObservationExtent::complete};
    const auto summary = qa::summarize_assignment_inventory(reference, observations);
    require(summary.status == qa::AssignmentInventoryStatus::summarized,
            "other_reference_not_summarized");
    require(summary.expected_count.value == 3 && summary.inventory_count.value == 3 &&
                summary.observed_assignment_count == 3 && summary.duplicate_observation_count == 1,
            "other_reference_inherited_or_inflated_expectation");
    require(summary.loaded_count.value == 2 && summary.pending_load_count.value == 1,
            "other_reference_loads_not_counted");
}

void verify_conflicting_duplicate() {
    auto first = assignment("pack", 7, qa::LoadState::loaded, "authored");
    auto conflict = first;
    conflict.load_state = qa::LoadState::pending;
    const std::array observations{first, conflict};
    const qa::AssignmentInventoryReference reference{
        "pack", qa::ExpectedAssignmentCountSource::observed_inventory, std::nullopt,
        qa::ObservationExtent::complete};
    const auto summary = qa::summarize_assignment_inventory(reference, observations);
    require(summary.status == qa::AssignmentInventoryStatus::conflicting_duplicate &&
                summary.observed_assignment_count == 1 && summary.duplicate_observation_count == 1,
            "conflicting_duplicate_not_preserved");
    require(!summary.loaded_count.value && !summary.pending_load_count.value &&
                summary.first_problem.value == first.id,
            "conflicting_duplicate_presented_as_unambiguous_load");
}

} // namespace

int main() {
    try {
        verify_initial_reference();
        verify_other_reference();
        verify_conflicting_duplicate();
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "qa_assignment_inventory_summary_failed: %s\n", error.what());
        return 1;
    }
}
