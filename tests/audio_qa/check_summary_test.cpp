#include "check_summary.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

qa::TakeTechnicalResult result(std::string id, qa::CheckTechnicalOutcome outcome,
                               bool restart = false, bool overlap = false) {
    return {std::move(id), outcome, "diagnostic", restart, overlap};
}

bool priority_is_4320() {
    const std::array all_complete{result("a", qa::CheckTechnicalOutcome::complete),
                                  result("b", qa::CheckTechnicalOutcome::complete)};
    const auto complete = qa::summarize_check_results(all_complete);
    if (!complete || complete->exit_code != 0 || complete->complete != 2)
        return false;

    const std::array with_incomplete{result("a", qa::CheckTechnicalOutcome::complete),
                                     result("b", qa::CheckTechnicalOutcome::incomplete)};
    const auto incomplete = qa::summarize_check_results(with_incomplete);
    if (!incomplete || incomplete->exit_code != 2 || incomplete->incomplete != 1)
        return false;

    const std::array with_invalid{result("a", qa::CheckTechnicalOutcome::incomplete),
                                  result("b", qa::CheckTechnicalOutcome::invalid_request)};
    const auto invalid = qa::summarize_check_results(with_invalid);
    if (!invalid || invalid->exit_code != 3 || invalid->invalid != 1)
        return false;

    const std::array with_preservation{result("a", qa::CheckTechnicalOutcome::preservation_failure),
                                       result("b", qa::CheckTechnicalOutcome::invalid_request),
                                       result("c", qa::CheckTechnicalOutcome::incomplete)};
    const auto preservation = qa::summarize_check_results(with_preservation);
    return preservation && preservation->exit_code == 4 &&
           preservation->preservation_failure == 1 && preservation->invalid == 1 &&
           preservation->incomplete == 1;
}

bool audible_observations_are_neutral() {
    const std::array without_symptom{result("main", qa::CheckTechnicalOutcome::complete)};
    const std::array with_symptom{result("main", qa::CheckTechnicalOutcome::complete, true, true)};
    const auto first = qa::summarize_check_results(without_symptom);
    const auto second = qa::summarize_check_results(with_symptom);
    return first && second && first->exit_code == 0 && second->exit_code == 0 &&
           second->takes.front().audible_restart_observed &&
           second->takes.front().audible_overlap_observed;
}

bool validation_and_formatting() {
    const std::array duplicates{result("same", qa::CheckTechnicalOutcome::complete),
                                result("same", qa::CheckTechnicalOutcome::incomplete)};
    if (qa::summarize_check_results(duplicates).has_value() ||
        qa::summarize_check_results(std::span<const qa::TakeTechnicalResult>{}).has_value())
        return false;
    const std::array values{result("main", qa::CheckTechnicalOutcome::incomplete, true)};
    const auto summary = qa::summarize_check_results(values);
    if (!summary)
        return false;
    const auto text = qa::format_check_summary(*summary);
    return text.find("exit_code=2") != std::string::npos &&
           text.find("outcome=incomplete") != std::string::npos &&
           text.find("audible_restart_observed=true") != std::string::npos;
}

} // namespace

int main() {
    const std::array unevaluated{qa::TakeTechnicalResult{
        "main", qa::CheckTechnicalOutcome::complete, "complete", std::nullopt, std::nullopt}};
    const auto summary = qa::summarize_check_results(unevaluated);
    if (!summary || qa::format_check_summary(*summary).find(
                        "audible_restart_observed=not_evaluated") == std::string::npos)
        return 1;
    return priority_is_4320() && audible_observations_are_neutral() && validation_and_formatting()
               ? 0
               : 1;
}
