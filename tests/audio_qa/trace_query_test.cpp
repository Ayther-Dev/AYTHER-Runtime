#include "trace_query.h"

#include <array>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

qa::Fact fact(std::string producer, std::uint64_t sequence,
              std::optional<qa::FactId> cause = std::nullopt) {
    qa::Fact result;
    result.id = {"run", std::move(producer), sequence};
    result.kind = "trace_fact";
    if (cause) {
        result.cause_ids.push_back(*cause);
    }
    return result;
}

qa::MixSpan span(std::string id, qa::FactId fact_id, std::uint64_t begin, std::uint64_t end) {
    qa::MixSpan result;
    result.run_id = "run";
    result.span_id = std::move(id);
    result.mix_range = {"engine_mix", 48000, begin, end};
    result.output_range = {
        qa::Availability::known, qa::SampleFrameRange{"engine_output", 48000, begin, end}, {}};
    result.facts.push_back(std::move(fact_id));
    return result;
}

void verify_explicit_path() {
    std::array<qa::Fact, 5> facts;
    facts[0] = fact("event", 1);
    facts[1] = fact("candidate", 1, facts[0].id);
    facts[2] = fact("selection", 1, facts[1].id);
    facts[3] = fact("playback", 1, facts[2].id);
    facts[4] = fact("mix", 1, facts[3].id);
    const std::array spans{span("mix-explicit", facts[4].id, 100, 132)};

    const auto result = qa::query_event_to_audio(facts[0].id, facts, spans);
    require(result.status == qa::EventAudioQueryStatus::found && result.routes.size() == 1,
            "explicit_route_not_found");
    const auto &route = result.routes.front();
    require(route.basis == qa::TraceRelationBasis::explicit_cause &&
                route.fact_path.size() == facts.size() &&
                route.fact_path.front() == facts.front().id &&
                route.fact_path.back() == facts.back().id,
            "explicit_links_not_returned");
    require(route.output_range.begin == 100 && route.output_range.end == 132 &&
                route.relation_reason == "cause_ids",
            "explicit_audio_range_not_returned");
}

void verify_temporal_path_is_labeled() {
    const std::array facts{fact("event", 1), fact("mix", 1)};
    const std::array spans{span("mix-temporal", facts[1].id, 200, 220)};
    const std::array links{
        qa::TemporalTraceLink{facts[0].id, "mix-temporal", "same_frame_sample_window"}};

    const auto result = qa::query_event_to_audio(facts[0].id, facts, spans, links);
    require(result.status == qa::EventAudioQueryStatus::found && result.routes.size() == 1,
            "temporal_route_not_found");
    const auto &route = result.routes.front();
    require(route.basis == qa::TraceRelationBasis::temporal_correlation &&
                route.fact_path == std::vector<qa::FactId>{facts[0].id} &&
                route.relation_reason == "same_frame_sample_window" &&
                route.output_range.begin == 200 && route.output_range.end == 220,
            "temporal_route_not_labeled");
}

} // namespace

int main() {
    try {
        verify_explicit_path();
        verify_temporal_path_is_labeled();
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "qa_trace_query_failed: %s\n", error.what());
        return 1;
    }
}
