#pragma once

#include "request_outcome.h"

namespace ayther::audio_qa {

// Spec 002, contracts.md C2 «Evidencia completa» (RF-2.13, RNF-5): what must have been kept
// and checked for the evidence of a traversal to be complete.
struct EvidenceParts {
    // Request, run, position, materials with SHA-256 and versions.
    bool identification{};
    // The effective conditions.
    bool conditions{};
    // Every position traversed (traversal.toml).
    bool positions{};
    // Every inspection control applied, readable.
    bool controls{};
    // The confirmed result (replay-result.toml).
    bool result{};
    // The QA data of every visited frame, without known losses.
    bool data_without_losses{};
    // Every fragment flushed durably at its confirmation point.
    bool fragments_flushed{};
    // Plan §8 P-14 (RNF-3): the traversal stayed within its approved limits. Unlike the parts
    // above it is kept unless a limit was exceeded.
    bool within_limits{true};
};

// Complete only with every part; otherwise `incomplete` with one reason per missing part,
// in the order of the fields above.
[[nodiscard]] EvidenceOutcome evaluate_evidence(const EvidenceParts &parts);

// Complete evidence of a cancellation or an inspection is not a take played in full.
[[nodiscard]] bool credits_full_take(const EvidenceOutcome &evidence, PlaybackKind playback,
                                     TraversalKind traversal) noexcept;

} // namespace ayther::audio_qa
