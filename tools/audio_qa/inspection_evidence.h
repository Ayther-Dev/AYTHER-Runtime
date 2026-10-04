#pragma once

#include "durable_file.h"
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

struct TraversalDocument {
    TraversalKind kind{TraversalKind::linear};
    bool linear_completed{};
    std::uint64_t frames_total{};
    std::vector<TraversalSegment> segments;
    std::vector<TraversalVisit> visits;
    std::vector<TraversalResume> resume_after;
    std::vector<TraversalInterruption> interruptions;
    bool operator==(const TraversalDocument &) const = default;
};

// Builds the document from what the Runtime reports, in order. A pause without
// navigation keeps the traversal linear; the first step that moves the position turns it
// into an inspection for good (RF-5.8).
class TraversalRecorder final {
  public:
    explicit TraversalRecorder(std::uint64_t frames_total) noexcept;
    void frame_played(std::uint64_t frame);
    void inspection(const InspectionEvent &event);
    [[nodiscard]] TraversalDocument document() const;

  private:
    TraversalDocument document_;
    bool after_visit_{};
    std::optional<std::uint64_t> last_frame_;
};

// The traversal a take had, from what its terminal and its inspection events say: the
// frames before the first event are linear, the events are visits, and the frames after
// the last one continue up to the last frame consumed.
[[nodiscard]] TraversalDocument traversal_of_take(std::uint64_t frames_total,
                                                  std::uint64_t frames_consumed,
                                                  std::span<const InspectionEvent> events);

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
