#include "mix_model.h"

#include <cstdio>
#include <limits>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        qa::Occurrence old_voice;
        old_voice.id = {"run", 1};
        old_voice.business_key = 101;
        old_voice.start_causes = {qa::PreexistingContext{"initial-state"}};
        auto new_voice = old_voice;
        new_voice.id.sequence = 2;
        new_voice.start_causes = {qa::FactId{"run", "session", 4}};
        require(qa::well_formed(old_voice) && qa::well_formed(new_voice) &&
                    old_voice.id != new_voice.id &&
                    old_voice.business_key == new_voice.business_key,
                "reused_key_confused_occurrences");
        const qa::SampleFrameRange a{"mix", 44100, 8, 64};
        const qa::SampleFrameRange b{"mix", 44100, 64, 72};
        require(qa::overlaps(a, b) == false, "touching_half_open_ranges_overlap");
        require(qa::overlaps(a, {"mix", 44100, 63, 72}) == true, "one_frame_overlap_lost");
        require(!qa::overlaps(a, {"output", 48000, 8, 64}).has_value(),
                "unmapped_timelines_compared");
        require(qa::overlaps(a, {"mix", 44100, 10, 10}) == false, "empty_range_overlaps");
        qa::MixSpan span;
        span.run_id = "run";
        span.span_id = "span-1";
        span.mix_range = a;
        span.original_audio = qa::OriginalAudio::present;
        qa::MixParticipant first;
        first.occurrence_id = old_voice.id;
        first.mix_range = a;
        first.gain_begin = {qa::Availability::known, 0.0F, {}};
        first.gain_end = first.gain_begin;
        auto second = first;
        second.occurrence_id = new_voice.id;
        span.participants = {first, second};
        require(qa::well_formed(span), "two_occurrences_or_zero_gain_rejected");
        span.participants[1].occurrence_id = old_voice.id;
        require(!qa::well_formed(span), "duplicate_occurrence_accepted");
        span.participants[1] = second;
        span.participants[1].mix_range.end = 65;
        require(!qa::well_formed(span), "participant_outside_span_accepted");
        span.participants[1] = second;
        span.participants[1].gain_end.value = std::numeric_limits<float>::quiet_NaN();
        require(!qa::well_formed(span), "non_finite_gain_accepted");
        span.participants.clear();
        for (std::size_t i = 0; i < qa::max_mix_participants; ++i) {
            auto p = first;
            p.occurrence_id.sequence = i + 1;
            span.participants.push_back(p);
        }
        require(qa::well_formed(span), "participant_limit_boundary_rejected");
        span.participants.push_back(second);
        require(!qa::well_formed(span), "participant_limit_ignored");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "qa_mix_model_test_failed: %s\n", error.what());
        return 1;
    }
}
