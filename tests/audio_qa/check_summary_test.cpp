#include "check_summary.h"

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

qa::TakeTechnicalResult result(std::string id, qa::CheckTechnicalOutcome outcome,
                               bool restart = false, bool overlap = false) {
    return {std::move(id), outcome, "diagnostic", restart, overlap, 0, {}, {}, {}};
}

// Results arrive in request order; spec 002 (RF-1.4) identifies them by position.
template <std::size_t N>
std::array<qa::TakeTechnicalResult, N> ordered(std::array<qa::TakeTechnicalResult, N> results) {
    for (std::size_t index = 0; index < N; ++index)
        results[index].position = index;
    return results;
}

bool priority_is_4320() {
    const auto all_complete = ordered(std::array{result("a", qa::CheckTechnicalOutcome::complete),
                                                 result("b", qa::CheckTechnicalOutcome::complete)});
    const auto complete = qa::summarize_check_results(all_complete);
    if (!complete || complete->exit_code != 0 || complete->complete != 2)
        return false;

    const auto with_incomplete =
        ordered(std::array{result("a", qa::CheckTechnicalOutcome::complete),
                           result("b", qa::CheckTechnicalOutcome::incomplete)});
    const auto incomplete = qa::summarize_check_results(with_incomplete);
    if (!incomplete || incomplete->exit_code != 2 || incomplete->incomplete != 1)
        return false;

    const auto with_invalid =
        ordered(std::array{result("a", qa::CheckTechnicalOutcome::incomplete),
                           result("b", qa::CheckTechnicalOutcome::invalid_request)});
    const auto invalid = qa::summarize_check_results(with_invalid);
    if (!invalid || invalid->exit_code != 3 || invalid->invalid != 1)
        return false;

    const auto with_preservation =
        ordered(std::array{result("a", qa::CheckTechnicalOutcome::preservation_failure),
                           result("b", qa::CheckTechnicalOutcome::invalid_request),
                           result("c", qa::CheckTechnicalOutcome::incomplete)});
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

// RF-1.4: the same take twice is a legitimate request; what identifies a result is its
// position, so a repeated or out-of-order position is rejected instead.
bool repeated_takes_are_positions() {
    const auto repeated = ordered(std::array{result("A", qa::CheckTechnicalOutcome::complete),
                                             result("B", qa::CheckTechnicalOutcome::complete),
                                             result("A", qa::CheckTechnicalOutcome::incomplete)});
    const auto summary = qa::summarize_check_results(repeated);
    if (!summary || summary->takes.size() != 3 || summary->complete != 2 ||
        summary->incomplete != 1)
        return false;
    const auto text = qa::format_check_summary(*summary);
    if (text.find("take=A position=0") == std::string::npos ||
        text.find("take=B position=1") == std::string::npos ||
        text.find("take=A position=2") == std::string::npos)
        return false;
    auto duplicated_position = repeated;
    duplicated_position[2].position = 0;
    return !qa::summarize_check_results(duplicated_position).has_value();
}

qa::TakeTechnicalResult take(std::size_t position, std::string playback, std::string traversal,
                             std::string evidence) {
    const bool complete = playback == "natural_end" && evidence == "complete";
    qa::TakeTechnicalResult value{"t.ayr",
                                  complete ? qa::CheckTechnicalOutcome::complete
                                           : qa::CheckTechnicalOutcome::incomplete,
                                  complete ? "replay_evidence_complete" : "diagnostic",
                                  std::nullopt,
                                  std::nullopt,
                                  position,
                                  std::move(playback),
                                  std::move(traversal),
                                  std::move(evidence)};
    return value;
}

// BR-064 (RF-2.4, RF-2.7): each take reports playback, traversal and evidence apart, and
// the summary says that a technical natural end is not a visual evaluation.
bool results_are_separate() {
    const std::array values{take(0, "natural_end", "inspection", "complete")};
    const auto summary = qa::summarize_check_results(values);
    if (!summary)
        return false;
    const auto text = qa::format_check_summary(*summary);
    return text.find("playback=natural_end traversal=inspection evidence=complete") !=
               std::string::npos &&
           text.find("visual_evaluation=not_evaluated") != std::string::npos &&
           text.find("note=technical_result_is_not_a_visual_evaluation") != std::string::npos;
}

// BR-065 (RF-2.12): the joint result credits a complete linear reproduction only when
// every take is linear, ended naturally and kept complete evidence.
bool joint_result() {
    const std::array linear{take(0, "natural_end", "linear", "complete"),
                            take(1, "natural_end", "linear", "complete")};
    const auto all_linear = qa::summarize_check_results(linear);
    if (!all_linear || !all_linear->linear_complete ||
        qa::format_check_summary(*all_linear).find("linear_complete=true") == std::string::npos)
        return false;
    const std::array<std::array<const char *, 3>, 6> spoilers{{
        {"natural_end", "inspection", "complete"},
        {"natural_end", "unknown", "complete"},
        {"natural_end", "linear", "incomplete"},
        {"cancelled", "linear", "incomplete"},
        {"failed", "linear", "incomplete"},
        {"not_started", "none", "incomplete"},
    }};
    for (const auto &spoiler : spoilers) {
        const std::array mixed{take(0, "natural_end", "linear", "complete"),
                               take(1, spoiler[0], spoiler[1], spoiler[2])};
        const auto summary = qa::summarize_check_results(mixed);
        if (!summary || summary->linear_complete)
            return false;
    }
    const std::array counted{take(0, "natural_end", "inspection", "complete"),
                             take(1, "cancelled", "linear", "incomplete"),
                             take(2, "failed", "linear", "incomplete"),
                             take(3, "not_started", "none", "incomplete")};
    const auto summary = qa::summarize_check_results(counted);
    return summary && summary->inspected == 1U && summary->cancelled == 1U &&
           summary->failed == 1U && summary->not_started == 1U;
}

bool validation_and_formatting() {
    if (qa::summarize_check_results(std::span<const qa::TakeTechnicalResult>{}).has_value())
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
    const std::array unevaluated{qa::TakeTechnicalResult{"main",
                                                         qa::CheckTechnicalOutcome::complete,
                                                         "complete",
                                                         std::nullopt,
                                                         std::nullopt,
                                                         0,
                                                         {},
                                                         {},
                                                         {}}};
    const auto summary = qa::summarize_check_results(unevaluated);
    if (!summary || qa::format_check_summary(*summary).find(
                        "audible_restart_observed=not_evaluated") == std::string::npos)
        return 1;
    return priority_is_4320() && audible_observations_are_neutral() &&
                   repeated_takes_are_positions() && validation_and_formatting() &&
                   results_are_separate() && joint_result()
               ? 0
               : 1;
}
