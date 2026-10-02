#include "check_summary.h"

#include "model_limits.h"

#include <algorithm>
#include <sstream>

namespace ayther::audio_qa {

namespace {

int exit_code(CheckTechnicalOutcome outcome) noexcept {
    switch (outcome) {
    case CheckTechnicalOutcome::complete:
        return 0;
    case CheckTechnicalOutcome::incomplete:
        return 2;
    case CheckTechnicalOutcome::invalid_request:
        return 3;
    case CheckTechnicalOutcome::preservation_failure:
        return 4;
    }
    return 3;
}

bool valid(const TakeTechnicalResult &result) noexcept {
    return !result.take_id.empty() && result.take_id.size() <= max_identity_bytes &&
           !result.diagnostic_code.empty() && result.diagnostic_code.size() <= max_identity_bytes;
}

} // namespace

std::optional<CheckTechnicalSummary>
summarize_check_results(std::span<const TakeTechnicalResult> results) {
    if (results.empty() || results.size() > max_request_takes)
        return std::nullopt;
    CheckTechnicalSummary summary;
    summary.takes.assign(results.begin(), results.end());
    for (std::size_t index = 0; index < results.size(); ++index) {
        if (!valid(results[index]) ||
            std::any_of(results.begin(), results.begin() + static_cast<std::ptrdiff_t>(index),
                        [&](const TakeTechnicalResult &previous) {
                            return previous.take_id == results[index].take_id;
                        }))
            return std::nullopt;
        summary.exit_code = (std::max)(summary.exit_code, exit_code(results[index].outcome));
        switch (results[index].outcome) {
        case CheckTechnicalOutcome::complete:
            ++summary.complete;
            break;
        case CheckTechnicalOutcome::incomplete:
            ++summary.incomplete;
            break;
        case CheckTechnicalOutcome::invalid_request:
            ++summary.invalid;
            break;
        case CheckTechnicalOutcome::preservation_failure:
            ++summary.preservation_failure;
            break;
        }
    }
    return summary;
}

std::string format_check_summary(const CheckTechnicalSummary &summary) {
    const auto observation = [](const std::optional<bool> value) {
        return value ? (*value ? "true" : "false") : "not_evaluated";
    };
    std::ostringstream output;
    output << "audio_qa_summary: exit_code=" << summary.exit_code
           << " complete=" << summary.complete << " incomplete=" << summary.incomplete
           << " invalid=" << summary.invalid
           << " preservation_failure=" << summary.preservation_failure;
    for (const auto &take : summary.takes)
        output << "\ntake=" << take.take_id
               << " outcome=" << check_technical_outcome_code(take.outcome)
               << " diagnostic=" << take.diagnostic_code
               << " audible_restart_observed=" << observation(take.audible_restart_observed)
               << " audible_overlap_observed=" << observation(take.audible_overlap_observed);
    return output.str();
}

std::string_view check_technical_outcome_code(CheckTechnicalOutcome outcome) noexcept {
    switch (outcome) {
    case CheckTechnicalOutcome::complete:
        return "complete";
    case CheckTechnicalOutcome::incomplete:
        return "incomplete";
    case CheckTechnicalOutcome::invalid_request:
        return "invalid_request";
    case CheckTechnicalOutcome::preservation_failure:
        return "preservation_failure";
    }
    return "invalid_request";
}

} // namespace ayther::audio_qa
