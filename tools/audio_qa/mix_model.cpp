#include "mix_model.h"

#include <cmath>
#include <string_view>

namespace ayther::audio_qa {
namespace {
bool identifier(std::string_view value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}
bool valid(const OccurrenceId &id) noexcept { return identifier(id.run_id) && id.sequence != 0; }
template <class T> bool valid(const Field<T> &field) noexcept {
    return consistent_availability(field);
}
bool valid(const Field<TrackPosition> &position) noexcept {
    return consistent_availability(position) &&
           (!position.value || position.value->sample_rate > 0);
}
bool valid(const Field<SampleFrameRange> &range) noexcept {
    return consistent_availability(range) && (!range.value || well_formed(*range.value));
}
bool valid(const Field<float> &gain) noexcept {
    return consistent_availability(gain) && (!gain.value || std::isfinite(*gain.value));
}
bool valid(const Field<std::string> &id) noexcept {
    return consistent_availability(id) && (!id.value || identifier(*id.value));
}
} // namespace

bool well_formed(const SampleFrameRange &range) noexcept {
    return identifier(range.timeline_id) && range.sample_rate > 0 && range.begin <= range.end;
}

std::optional<bool> overlaps(const SampleFrameRange &left, const SampleFrameRange &right) noexcept {
    if (!well_formed(left) || !well_formed(right) || left.timeline_id != right.timeline_id ||
        left.sample_rate != right.sample_rate) {
        return std::nullopt;
    }
    return left.begin < left.end && right.begin < right.end && left.begin < right.end &&
           right.begin < left.end;
}

bool well_formed(const Occurrence &occurrence) noexcept {
    if (!valid(occurrence.id) || !valid(occurrence.track_id) || !valid(occurrence.assignment_id) ||
        !valid(occurrence.position) || !valid(occurrence.loop_region) || !valid(occurrence.gain) ||
        !valid(occurrence.muted) || !valid(occurrence.final_reason) ||
        occurrence.start_causes.size() > max_fact_causes) {
        return false;
    }
    for (const auto &cause : occurrence.start_causes) {
        if (const auto *fact = std::get_if<FactId>(&cause)) {
            if (fact->run_id != occurrence.id.run_id || !identifier(fact->producer_id) ||
                fact->producer_sequence == 0) {
                return false;
            }
        } else if (!identifier(std::get<PreexistingContext>(cause).context_id)) {
            return false;
        }
    }
    return true;
}

bool well_formed(const MixSpan &span) noexcept {
    if (!identifier(span.run_id) || !identifier(span.span_id) || !well_formed(span.mix_range) ||
        span.mix_range.begin == span.mix_range.end || !valid(span.output_range) ||
        span.original_audio < OriginalAudio::unknown ||
        span.original_audio > OriginalAudio::absent ||
        span.participants.size() > max_mix_participants || span.facts.size() > max_fact_causes) {
        return false;
    }
    for (std::size_t i = 0; i < span.participants.size(); ++i) {
        const auto &p = span.participants[i];
        if (!valid(p.occurrence_id) || p.occurrence_id.run_id != span.run_id ||
            !well_formed(p.mix_range) || p.mix_range.begin == p.mix_range.end ||
            p.mix_range.timeline_id != span.mix_range.timeline_id ||
            p.mix_range.sample_rate != span.mix_range.sample_rate ||
            p.mix_range.begin < span.mix_range.begin || p.mix_range.end > span.mix_range.end ||
            !valid(p.position_begin) || !valid(p.position_end) || !valid(p.gain_begin) ||
            !valid(p.gain_end) || !valid(p.muted)) {
            return false;
        }
        for (std::size_t earlier = 0; earlier < i; ++earlier) {
            if (span.participants[earlier].occurrence_id == p.occurrence_id) {
                return false;
            }
        }
    }
    for (const auto &fact : span.facts) {
        if (fact.run_id != span.run_id || !identifier(fact.producer_id) ||
            fact.producer_sequence == 0) {
            return false;
        }
    }
    return true;
}

} // namespace ayther::audio_qa
