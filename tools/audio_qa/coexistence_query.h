#pragma once

#include "mix_model.h"

#include <span>
#include <string>
#include <vector>

namespace ayther::audio_qa {

struct TransitionMarker {
    std::string run_id;
    std::string marker_id;
    SampleFrameRange range;
    Field<FactId> source_fact;
};

enum class VoiceStartProvenance { unknown, explicit_fact, preexisting_context, mixed };
struct CoexistingVoice {
    OccurrenceId occurrence_id;
    Field<std::string> assignment_id;
    VoiceStartProvenance start_provenance{VoiceStartProvenance::unknown};
    std::vector<Cause> start_causes;
    std::vector<SampleFrameRange> overlapping_intervals;
};

enum class CoexistenceQueryStatus {
    found,
    no_coexistence,
    invalid_marker,
    invalid_occurrence,
    invalid_mix_span,
    missing_occurrence,
    capacity_exceeded
};
struct CoexistenceQueryResult {
    CoexistenceQueryStatus status{CoexistenceQueryStatus::no_coexistence};
    TransitionMarker marker;
    std::vector<CoexistingVoice> voices;
};

[[nodiscard]] CoexistenceQueryResult query_coexistence(const TransitionMarker &marker,
                                                       std::span<const Occurrence> occurrences,
                                                       std::span<const MixSpan> mix_spans);

} // namespace ayther::audio_qa
