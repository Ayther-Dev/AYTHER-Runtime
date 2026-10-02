#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ayther::audio_qa {

enum class Admission { pending, accepted, rejected };
enum class Phase { preparing, ready, playing, closing, closed };
enum class PlaybackResult { not_started, in_progress, natural_end, cancelled, error };
enum class EvidenceResult { pending, in_progress, complete, incomplete };
enum class EquivalenceResult { unknown, verified, incomplete };

struct Request {
    std::string request_id;
    std::string session_id;
    // Identity of the immutable conditions document; never a mutable configuration path.
    std::string conditions_id;
    std::vector<std::string> take_ids;
    Admission admission{Admission::pending};
    bool operator==(const Request &) const = default;
};

struct Run {
    std::string run_id;
    std::string request_id;
    std::string take_id;
    Phase phase{Phase::preparing};
    PlaybackResult playback_result{PlaybackResult::not_started};
    EvidenceResult evidence_result{EvidenceResult::pending};
    EquivalenceResult equivalence_result{EquivalenceResult::unknown};
    std::optional<std::uint64_t> last_executed_frame;
    // Exclusive boundary in sample_frames, not an interleaved scalar sample index.
    std::optional<std::uint64_t> last_durable_sample;
    bool cessation_confirmed{};
    bool operator==(const Run &) const = default;
};

} // namespace ayther::audio_qa
