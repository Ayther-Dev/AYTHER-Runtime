#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

// Spec 002, contracts.md C1-3 and C1-6 (RF-2.3, RF-2.9): the Runtime side of the live state
// of protocol 1.1, a `session_status` (type 4) on the data channel. The supervisor reads it
// with decode_session_status; it is never evidence.
struct ReplayStateMessage {
    std::string run_id;
    std::size_t take_position{};
    // preparing|playing|pausing|paused|recovering|interrupted|ended_paused|closing
    std::string phase;
    // Absent before the first completed frame.
    std::optional<std::uint64_t> frame;
    std::uint64_t frames_total{};
    bool overlay_visible{};
    // Only with the phase `interrupted`.
    std::string interruption_cause;
};

struct RunOpenedMessage {
    std::string run_id;
    std::size_t take_position{};
};

enum class SessionStatusEncodeError { invalid_state, too_large };

using EncodedSessionStatus = std::variant<std::vector<std::byte>, SessionStatusEncodeError>;

[[nodiscard]] EncodedSessionStatus encode_replay_state(const ReplayStateMessage &state,
                                                       std::uint64_t sequence);
// C1-6: the run of a post-end inspection; its traversal is always post_end_inspection.
[[nodiscard]] EncodedSessionStatus encode_run_opened(const RunOpenedMessage &opened,
                                                     std::uint64_t sequence);

} // namespace ayther::audio_qa
