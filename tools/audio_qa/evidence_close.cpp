#include "evidence_close.h"

namespace ayther::audio_qa {

bool EvidenceClose::finish(EvidenceCloseReport report, RunLifecycle &lifecycle) noexcept {
    if (report_.has_value()) {
        return false;
    }
    const auto result = report.failure == EvidenceCloseFailure::none ? EvidenceResult::complete
                                                                     : EvidenceResult::incomplete;
    if (lifecycle.finish_closing(result) != RunTransitionError::none) {
        return false;
    }
    report_ = report;
    return true;
}

const std::optional<EvidenceCloseReport> &EvidenceClose::report() const noexcept { return report_; }

} // namespace ayther::audio_qa
