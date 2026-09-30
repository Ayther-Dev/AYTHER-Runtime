#include "coexistence_query.h"

#include <algorithm>
#include <iterator>
#include <string_view>
#include <variant>

namespace ayther::audio_qa {
namespace {

bool identifier(std::string_view value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}

VoiceStartProvenance provenance(std::span<const Cause> causes) noexcept {
    bool explicit_fact = false;
    bool preexisting = false;
    for (const auto &cause : causes) {
        explicit_fact = explicit_fact || std::holds_alternative<FactId>(cause);
        preexisting = preexisting || std::holds_alternative<PreexistingContext>(cause);
    }
    if (explicit_fact && preexisting) {
        return VoiceStartProvenance::mixed;
    }
    if (explicit_fact) {
        return VoiceStartProvenance::explicit_fact;
    }
    if (preexisting) {
        return VoiceStartProvenance::preexisting_context;
    }
    return VoiceStartProvenance::unknown;
}

} // namespace

CoexistenceQueryResult query_coexistence(const TransitionMarker &marker,
                                         std::span<const Occurrence> occurrences,
                                         std::span<const MixSpan> mix_spans) {
    CoexistenceQueryResult result;
    result.marker = marker;
    const bool source_valid =
        consistent_availability(marker.source_fact) &&
        (!marker.source_fact.value || (marker.source_fact.value->run_id == marker.run_id &&
                                       identifier(marker.source_fact.value->producer_id) &&
                                       marker.source_fact.value->producer_sequence != 0));
    if (!identifier(marker.run_id) || !identifier(marker.marker_id) || !well_formed(marker.range) ||
        marker.range.begin == marker.range.end || !source_valid) {
        result.status = CoexistenceQueryStatus::invalid_marker;
        return result;
    }
    if (occurrences.size() > max_reference_materials ||
        mix_spans.size() > max_reference_materials) {
        result.status = CoexistenceQueryStatus::capacity_exceeded;
        return result;
    }
    for (std::size_t index = 0; index < occurrences.size(); ++index) {
        if (!well_formed(occurrences[index])) {
            result.status = CoexistenceQueryStatus::invalid_occurrence;
            return result;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (occurrences[previous].id == occurrences[index].id) {
                result.status = CoexistenceQueryStatus::invalid_occurrence;
                return result;
            }
        }
    }
    for (const auto &span : mix_spans) {
        if (!well_formed(span)) {
            result.status = CoexistenceQueryStatus::invalid_mix_span;
            return result;
        }
        if (span.run_id != marker.run_id) {
            continue;
        }
        for (const auto &participant : span.participants) {
            if (participant.mix_range.timeline_id != marker.range.timeline_id ||
                participant.mix_range.sample_rate != marker.range.sample_rate) {
                continue;
            }
            const auto begin = (std::max)(participant.mix_range.begin, marker.range.begin);
            const auto end = (std::min)(participant.mix_range.end, marker.range.end);
            if (begin >= end) {
                continue;
            }
            const auto occurrence =
                std::find_if(occurrences.begin(), occurrences.end(), [&](const Occurrence &item) {
                    return item.id == participant.occurrence_id;
                });
            if (occurrence == occurrences.end()) {
                result.status = CoexistenceQueryStatus::missing_occurrence;
                result.voices.clear();
                return result;
            }
            auto voice = std::find_if(
                result.voices.begin(), result.voices.end(),
                [&](const CoexistingVoice &item) { return item.occurrence_id == occurrence->id; });
            if (voice == result.voices.end()) {
                result.voices.push_back({occurrence->id,
                                         occurrence->assignment_id,
                                         provenance(occurrence->start_causes),
                                         occurrence->start_causes,
                                         {}});
                voice = std::prev(result.voices.end());
            }
            voice->overlapping_intervals.push_back(
                {marker.range.timeline_id, marker.range.sample_rate, begin, end});
        }
    }
    result.status = result.voices.size() > 1 ? CoexistenceQueryStatus::found
                                             : CoexistenceQueryStatus::no_coexistence;
    return result;
}

} // namespace ayther::audio_qa
