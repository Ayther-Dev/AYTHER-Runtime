#pragma once

#include "audio_integrity.h"
#include "durable_file.h"
#include "inspection_fact_builder.h"
#include "request_outcome.h"
#include "runtime_protocol_v11.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

// Spec 002, plan §4.6 and contracts.md C2 (RF-5.8, RF-2.13): `runs/<run>/traversal.toml`
// 1.1. The linear part of a take is kept as compact segments; every inspection control is
// an explicit visit, repetitions included.
struct TraversalSegment {
    std::uint64_t from{};
    std::uint64_t to{};
    std::string mode;
    bool operator==(const TraversalSegment &) const = default;
};

struct TraversalVisit {
    std::uint64_t seq{};
    std::uint64_t frame{};
    std::string control;
    bool operator==(const TraversalVisit &) const = default;
};

// Playback continued after the visit `seq`.
struct TraversalResume {
    std::uint64_t seq{};
    std::uint64_t from_frame{};
    std::uint64_t to{};
    std::string mode;
    bool operator==(const TraversalResume &) const = default;
};

struct TraversalInterruption {
    std::uint64_t seq{};
    std::uint64_t frame{};
    bool operator==(const TraversalInterruption &) const = default;
};

// Spec 002, DI-14 (evidence 1.2): the audio of one linear segment. The Runtime opens a PCM
// segment at the start of the take and at every resume; its frames are the linear stretches
// of the traversal played in it, and its samples the PCM kept for it, contiguous. Between two
// segments the line may jump: a navigation produced its frames silently.
struct TraversalAudioSegment {
    std::uint64_t segment{};
    std::uint64_t frame_from{};
    std::uint64_t frame_to{};
    std::string timeline;
    std::uint32_t sample_rate{};
    std::uint64_t sample_begin{};
    std::uint64_t sample_end{};
    std::uint64_t pcm_blocks{};
    bool operator==(const TraversalAudioSegment &) const = default;
};

struct TraversalDocument {
    TraversalKind kind{TraversalKind::linear};
    bool linear_completed{};
    std::uint64_t frames_total{};
    std::vector<TraversalSegment> segments;
    std::vector<TraversalVisit> visits;
    std::vector<TraversalResume> resume_after;
    std::vector<TraversalInterruption> interruptions;
    // DI-14: absent in a traversal 1.1, whose audio segments are unknown.
    std::optional<std::vector<TraversalAudioSegment>> audio_segments;
    // DI-15: the Engine facts each recovery excluded, per producer. Absent in a traversal 1.1
    // or in an earlier 1.2: nothing was declared, and a gap in its facts stays a loss.
    std::optional<std::vector<FactExclusion>> fact_exclusions;
    bool operator==(const TraversalDocument &) const = default;
};

// Plan §8 P-14 (RNF-3): the inspection visits one traversal keeps.
inline constexpr std::uint64_t max_traversal_visits = 100'000U;

// Builds the document from what the Runtime reports, in order. A pause without
// navigation keeps the traversal linear; the first step that moves the position turns it
// into an inspection for good (RF-5.8). Above `max_traversal_visits` a visit is not kept:
// the traversal is marked as over its limit and the visits already kept stay as they were.
class TraversalRecorder final {
  public:
    explicit TraversalRecorder(std::uint64_t frames_total) noexcept;
    void frame_played(std::uint64_t frame);
    void inspection(const InspectionEvent &event);
    [[nodiscard]] TraversalDocument document() const;
    [[nodiscard]] bool visit_limit_exceeded() const noexcept;

  private:
    TraversalDocument document_;
    bool after_visit_{};
    bool visit_limit_exceeded_{};
    std::optional<std::uint64_t> last_frame_;
};

// The traversal a take had, from what its terminal and its inspection events say: the
// frames before the first event are linear, the events are visits, and the frames after
// the last one continue up to the last frame consumed.
[[nodiscard]] TraversalDocument traversal_of_take(std::uint64_t frames_total,
                                                  std::uint64_t frames_consumed,
                                                  std::span<const InspectionEvent> events);

// P-14: whether the events hold more visits than one traversal keeps.
[[nodiscard]] bool exceeds_visit_limit(std::span<const InspectionEvent> events) noexcept;

// DI-14: why the PCM kept does not match the linear segments of the traversal.
enum class AudioSegmentsError { pcm_without_frames, frames_without_pcm };
using AudioSegmentsResult = std::variant<std::vector<TraversalAudioSegment>, AudioSegmentsError>;

// The audio segments of a take: segment 0 holds the linear stretch from the start, and the
// stretches after the n-th `resume` belong to segment n. Each segment with frames needs its
// PCM, and PCM needs frames.
[[nodiscard]] AudioSegmentsResult audio_segments_of_take(const TraversalDocument &traversal,
                                                         std::span<const InspectionEvent> events,
                                                         std::span<const PcmSegmentInterval> pcm);

[[nodiscard]] std::string format_traversal(const TraversalDocument &document);

enum class TraversalReadError { missing, unreadable, unsupported_version, invalid };
using TraversalReadResult = std::variant<TraversalDocument, TraversalReadError>;

[[nodiscard]] TraversalReadResult parse_traversal(std::string_view text);
[[nodiscard]] TraversalReadResult read_traversal(const std::filesystem::path &path);
// Rewritten durably at every confirmation point (temporary file, flush and rename).
[[nodiscard]] DurablePublishResult write_traversal(const std::filesystem::path &path,
                                                   const TraversalDocument &document,
                                                   DurablePublicationLimits limits = {});

} // namespace ayther::audio_qa
