#include "coexistence_query.h"

#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

qa::Occurrence occurrence(std::uint64_t sequence, qa::Cause cause) {
    qa::Occurrence result;
    result.id = {"run", sequence};
    result.assignment_id = {qa::Availability::known, "assignment-" + std::to_string(sequence), {}};
    result.start_causes.push_back(std::move(cause));
    return result;
}

qa::MixParticipant participant(const qa::OccurrenceId &id, std::uint64_t begin, std::uint64_t end) {
    qa::MixParticipant result;
    result.occurrence_id = id;
    result.mix_range = {"engine_mix", 48000, begin, end};
    return result;
}

void verify_simultaneous_and_preexisting() {
    const qa::FactId transition_fact{"run", "transition", 1};
    const qa::FactId trigger_fact{"run", "playback", 1};
    const std::array occurrences{occurrence(1, trigger_fact),
                                 occurrence(2, qa::PreexistingContext{"fresh_start_context"})};
    qa::MixSpan span;
    span.run_id = "run";
    span.span_id = "coexistence";
    span.mix_range = {"engine_mix", 48000, 100, 130};
    span.output_range = {
        qa::Availability::known, qa::SampleFrameRange{"engine_output", 48000, 100, 130}, {}};
    span.participants = {participant(occurrences[0].id, 100, 130),
                         participant(occurrences[1].id, 105, 125)};
    const qa::TransitionMarker marker{"run",
                                      "screen_transition",
                                      {"engine_mix", 48000, 110, 120},
                                      {qa::Availability::known, transition_fact, {}}};

    const std::array spans{span};
    const auto result = qa::query_coexistence(marker, occurrences, spans);
    require(result.status == qa::CoexistenceQueryStatus::found && result.voices.size() == 2,
            "simultaneous_voices_not_found");
    require(result.marker.marker_id == "screen_transition" &&
                result.marker.source_fact.value == transition_fact &&
                result.marker.range.begin == 110 && result.marker.range.end == 120,
            "transition_provenance_or_interval_lost");
    require(result.voices[0].start_provenance == qa::VoiceStartProvenance::explicit_fact &&
                result.voices[0].start_causes == std::vector<qa::Cause>{trigger_fact},
            "explicit_trigger_not_preserved");
    require(
        result.voices[1].start_provenance == qa::VoiceStartProvenance::preexisting_context &&
            std::holds_alternative<qa::PreexistingContext>(result.voices[1].start_causes.front()),
        "preexisting_context_replaced_by_trigger");
    for (const auto &voice : result.voices) {
        require(voice.overlapping_intervals.size() == 1 &&
                    voice.overlapping_intervals.front().begin == 110 &&
                    voice.overlapping_intervals.front().end == 120,
                "coexistence_interval_not_preserved");
    }
}

} // namespace

int main() {
    try {
        verify_simultaneous_and_preexisting();
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "qa_coexistence_query_failed: %s\n", error.what());
        return 1;
    }
}
