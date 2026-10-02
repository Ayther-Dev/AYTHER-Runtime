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

struct TakeTechnicalResult {
    std::string take_id;
    CheckTechnicalOutcome outcome{CheckTechnicalOutcome::incomplete};
    std::string diagnostic_code;
    std::optional<bool> audible_restart_observed;
    std::optional<bool> audible_overlap_observed;
};

struct CheckTechnicalSummary {
    int exit_code{};
    std::size_t complete{};
    std::size_t incomplete{};
    std::size_t invalid{};
    std::size_t preservation_failure{};
    std::vector<TakeTechnicalResult> takes;
};

[[nodiscard]] std::optional<CheckTechnicalSummary>
summarize_check_results(std::span<const TakeTechnicalResult> results);
[[nodiscard]] std::string format_check_summary(const CheckTechnicalSummary &summary);
[[nodiscard]] std::string_view check_technical_outcome_code(CheckTechnicalOutcome outcome) noexcept;

} // namespace ayther::audio_qa
