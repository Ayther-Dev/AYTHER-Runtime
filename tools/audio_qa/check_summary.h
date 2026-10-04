#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ayther::audio_qa {

enum class CheckTechnicalOutcome {
    complete,
    incomplete,
    invalid_request,
    preservation_failure,
};

// Spec 002 (RF-1.4): a take result is identified by its position in the request;
// the same take path may legitimately appear at several positions.
//
// Spec 002 (RF-2.4, RF-2.7, RF-2.12): playback, traversal and evidence are reported
// apart. `playback` is natural_end, cancelled, failed, interrupted or not_started;
// `traversal` is linear, inspection, post_end_inspection, unknown (a Runtime that did not
// report it) or none (a take that did not start); `evidence` is complete or incomplete.
// Empty values come from callers that predate them and are reported as unknown.
struct TakeTechnicalResult {
    std::string take_id;
    CheckTechnicalOutcome outcome{CheckTechnicalOutcome::incomplete};
    std::string diagnostic_code;
    std::optional<bool> audible_restart_observed;
    std::optional<bool> audible_overlap_observed;
    std::size_t position{};
    std::string playback;
    std::string traversal;
    std::string evidence;
};

struct CheckTechnicalSummary {
    int exit_code{};
    std::size_t complete{};
    std::size_t incomplete{};
    std::size_t invalid{};
    std::size_t preservation_failure{};
    // RF-2.12: the joint result names the inspected, cancelled, failed and not started
    // takes, and only credits a complete linear reproduction when every take is linear,
    // ended naturally and kept complete evidence.
    std::size_t inspected{};
    std::size_t cancelled{};
    std::size_t failed{};
    std::size_t not_started{};
    bool linear_complete{};
    std::vector<TakeTechnicalResult> takes;
};

[[nodiscard]] std::optional<CheckTechnicalSummary>
summarize_check_results(std::span<const TakeTechnicalResult> results);
[[nodiscard]] std::string format_check_summary(const CheckTechnicalSummary &summary);
[[nodiscard]] std::string_view check_technical_outcome_code(CheckTechnicalOutcome outcome) noexcept;

} // namespace ayther::audio_qa
