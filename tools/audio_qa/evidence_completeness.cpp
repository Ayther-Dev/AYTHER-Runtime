#include "evidence_completeness.h"

#include <array>
#include <utility>

namespace ayther::audio_qa {

EvidenceOutcome evaluate_evidence(const EvidenceParts &parts) {
    const std::array<std::pair<bool, const char *>, 7> checks{{
        {parts.identification, "identification_missing"},
        {parts.conditions, "conditions_missing"},
        {parts.positions, "positions_missing"},
        {parts.controls, "controls_missing"},
        {parts.result, "result_missing"},
        {parts.data_without_losses, "data_lost"},
        {parts.fragments_flushed, "fragments_not_flushed"},
    }};
    EvidenceOutcome evidence;
    for (const auto &[kept, reason] : checks)
        if (!kept)
            evidence.reasons.emplace_back(reason);
    evidence.complete = evidence.reasons.empty();
    return evidence;
}

bool credits_full_take(const EvidenceOutcome &evidence, PlaybackKind playback,
                       TraversalKind traversal) noexcept {
    return evidence.complete && playback == PlaybackKind::natural_end &&
           traversal == TraversalKind::linear;
}

} // namespace ayther::audio_qa
