#include "trace_query.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

namespace ayther::audio_qa {
namespace {

bool identifier(std::string_view value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}

std::optional<std::size_t> fact_index(std::span<const Fact> facts, const FactId &id) noexcept {
    for (std::size_t index = 0; index < facts.size(); ++index) {
        if (facts[index].id == id) {
            return index;
        }
    }
    return std::nullopt;
}

bool directly_caused_by(const Fact &fact, const FactId &cause) noexcept {
    return std::any_of(fact.cause_ids.begin(), fact.cause_ids.end(), [&](const Cause &candidate) {
        const auto *id = std::get_if<FactId>(&candidate);
        return id != nullptr && *id == cause;
    });
}

std::vector<FactId> reconstruct_path(std::span<const Fact> facts,
                                     std::span<const std::size_t> parents, std::size_t origin,
                                     std::size_t target) {
    std::vector<FactId> reversed;
    for (auto current = target;; current = parents[current]) {
        reversed.push_back(facts[current].id);
        if (current == origin) {
            break;
        }
    }
    std::reverse(reversed.begin(), reversed.end());
    return reversed;
}

} // namespace

EventAudioQueryResult query_event_to_audio(const FactId &event_id, std::span<const Fact> facts,
                                           std::span<const MixSpan> mix_spans,
                                           std::span<const TemporalTraceLink> temporal_links) {
    EventAudioQueryResult result;
    if (facts.size() > max_reference_materials || mix_spans.size() > max_reference_materials ||
        temporal_links.size() > max_reference_materials) {
        result.status = EventAudioQueryStatus::capacity_exceeded;
        return result;
    }

    for (std::size_t index = 0; index < facts.size(); ++index) {
        if (!well_formed(facts[index])) {
            result.status = EventAudioQueryStatus::invalid_fact;
            return result;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (facts[previous].id == facts[index].id) {
                result.status = EventAudioQueryStatus::duplicate_fact;
                return result;
            }
        }
    }
    const auto origin = fact_index(facts, event_id);
    if (!origin) {
        result.status = EventAudioQueryStatus::invalid_origin;
        return result;
    }
    for (const auto &fact : facts) {
        for (const auto &cause : fact.cause_ids) {
            const auto *internal = std::get_if<FactId>(&cause);
            if (internal != nullptr && !fact_index(facts, *internal)) {
                result.status = EventAudioQueryStatus::invalid_fact;
                return result;
            }
        }
    }
    for (const auto &span : mix_spans) {
        if (!well_formed(span)) {
            result.status = EventAudioQueryStatus::invalid_mix_span;
            return result;
        }
        for (const auto &fact : span.facts) {
            if (!fact_index(facts, fact)) {
                result.status = EventAudioQueryStatus::invalid_mix_span;
                return result;
            }
        }
    }

    const auto missing_parent = facts.size();
    std::vector<std::size_t> parents(facts.size(), missing_parent);
    std::vector<std::size_t> queue;
    queue.reserve(facts.size());
    parents[*origin] = *origin;
    queue.push_back(*origin);
    for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) {
        const auto current = queue[cursor];
        for (std::size_t candidate = 0; candidate < facts.size(); ++candidate) {
            if (parents[candidate] == missing_parent &&
                directly_caused_by(facts[candidate], facts[current].id)) {
                parents[candidate] = current;
                queue.push_back(candidate);
            }
        }
    }

    bool related_range_unavailable = false;
    for (const auto &span : mix_spans) {
        if (span.run_id != event_id.run_id) {
            continue;
        }
        std::optional<std::size_t> target;
        for (const auto &fact : span.facts) {
            const auto index = fact_index(facts, fact);
            if (index && parents[*index] != missing_parent) {
                target = index;
                break;
            }
        }
        if (!target) {
            continue;
        }
        if (!span.output_range.value) {
            related_range_unavailable = true;
            continue;
        }
        result.routes.push_back({TraceRelationBasis::explicit_cause,
                                 reconstruct_path(facts, parents, *origin, *target), span.span_id,
                                 *span.output_range.value, "cause_ids"});
    }

    for (const auto &link : temporal_links) {
        if (!identifier(link.mix_span_id) || !identifier(link.reason_code)) {
            result.status = EventAudioQueryStatus::invalid_temporal_link;
            result.routes.clear();
            return result;
        }
        if (link.event_id != event_id) {
            continue;
        }
        const auto span =
            std::find_if(mix_spans.begin(), mix_spans.end(), [&](const MixSpan &candidate) {
                return candidate.run_id == event_id.run_id && candidate.span_id == link.mix_span_id;
            });
        if (span == mix_spans.end()) {
            result.status = EventAudioQueryStatus::invalid_temporal_link;
            result.routes.clear();
            return result;
        }
        const bool already_explicit = std::any_of(
            result.routes.begin(), result.routes.end(),
            [&](const EventAudioRoute &route) { return route.mix_span_id == span->span_id; });
        if (already_explicit) {
            continue;
        }
        if (!span->output_range.value) {
            related_range_unavailable = true;
            continue;
        }
        result.routes.push_back({TraceRelationBasis::temporal_correlation,
                                 {event_id},
                                 span->span_id,
                                 *span->output_range.value,
                                 link.reason_code});
    }

    if (!result.routes.empty()) {
        result.status = EventAudioQueryStatus::found;
    } else if (related_range_unavailable) {
        result.status = EventAudioQueryStatus::range_unavailable;
    }
    return result;
}

} // namespace ayther::audio_qa
