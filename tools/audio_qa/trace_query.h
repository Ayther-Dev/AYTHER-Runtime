#pragma once

#include "mix_model.h"

#include <span>
#include <string>
#include <vector>

namespace ayther::audio_qa {

struct TemporalTraceLink {
    FactId event_id;
    std::string mix_span_id;
    std::string reason_code;
};

enum class TraceRelationBasis { explicit_cause, temporal_correlation };
struct EventAudioRoute {
    TraceRelationBasis basis{TraceRelationBasis::explicit_cause};
    std::vector<FactId> fact_path;
    std::string mix_span_id;
    SampleFrameRange output_range;
    std::string relation_reason;
    bool operator==(const EventAudioRoute &) const = default;
};

enum class EventAudioQueryStatus {
    found,
    not_found,
    invalid_origin,
    invalid_fact,
    duplicate_fact,
    invalid_mix_span,
    invalid_temporal_link,
    capacity_exceeded,
    range_unavailable
};
struct EventAudioQueryResult {
    EventAudioQueryStatus status{EventAudioQueryStatus::not_found};
    std::vector<EventAudioRoute> routes;
};

[[nodiscard]] EventAudioQueryResult
query_event_to_audio(const FactId &event_id, std::span<const Fact> facts,
                     std::span<const MixSpan> mix_spans,
                     std::span<const TemporalTraceLink> temporal_links = {});

} // namespace ayther::audio_qa
