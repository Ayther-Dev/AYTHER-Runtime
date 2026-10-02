#pragma once

#include "fact_model.h"

namespace ayther::audio_qa {

struct OccurrenceId {
    std::string run_id;
    std::uint64_t sequence{};
    bool operator==(const OccurrenceId &) const = default;
};

struct TrackPosition {
    std::uint64_t sample_frame{};
    std::uint32_t sample_rate{};
    bool operator==(const TrackPosition &) const = default;
};

struct SampleFrameRange {
    std::string timeline_id;
    std::uint32_t sample_rate{};
    std::uint64_t begin{};
    std::uint64_t end{};
    bool operator==(const SampleFrameRange &) const = default;
};

struct Occurrence {
    OccurrenceId id;
    std::uint64_t business_key{};
    Field<std::string> track_id;
    Field<std::string> assignment_id;
    std::vector<Cause> start_causes;
    Field<TrackPosition> position;
    Field<SampleFrameRange> loop_region;
    Field<float> gain;
    Field<bool> muted;
    Field<std::string> final_reason;
};

struct MixParticipant {
    OccurrenceId occurrence_id;
    SampleFrameRange mix_range;
    Field<TrackPosition> position_begin;
    Field<TrackPosition> position_end;
    Field<float> gain_begin;
    Field<float> gain_end;
    Field<bool> muted;
};

enum class OriginalAudio { unknown, present, suppressed, absent };

struct MixSpan {
    std::string run_id;
    std::string span_id;
    SampleFrameRange mix_range;
    Field<SampleFrameRange> output_range;
    OriginalAudio original_audio{OriginalAudio::unknown};
    std::vector<MixParticipant> participants;
    std::vector<FactId> facts;
};

[[nodiscard]] bool well_formed(const SampleFrameRange &range) noexcept;
[[nodiscard]] bool well_formed(const Occurrence &occurrence) noexcept;
[[nodiscard]] bool well_formed(const MixSpan &span) noexcept;
// No mapping is inferred between different timelines or rates.
[[nodiscard]] std::optional<bool> overlaps(const SampleFrameRange &left,
                                           const SampleFrameRange &right) noexcept;

} // namespace ayther::audio_qa
