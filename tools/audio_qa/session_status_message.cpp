#include "session_status_message.h"

#include "model_limits.h"
#include "protocol_header.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <sstream>
#include <string_view>

namespace ayther::audio_qa {
namespace {

constexpr std::array<std::string_view, 8> replay_phases{"preparing",    "playing",    "pausing",
                                                        "paused",       "recovering", "interrupted",
                                                        "ended_paused", "closing"};

bool identity(const std::string &value) {
    return !value.empty() && value.size() <= max_identity_bytes;
}

bool fits(std::uint64_t value) {
    return value <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
}

EncodedSessionStatus frame(const toml::table &document, std::uint64_t sequence) {
    if (sequence == 0U)
        return SessionStatusEncodeError::invalid_state;
    std::ostringstream text;
    text << document;
    const auto payload = text.str();
    if (payload.empty() || payload.size() > max_protocol_payload_bytes)
        return SessionStatusEncodeError::too_large;
    const auto header = encode_protocol_header(
        {MessageType::session_status, static_cast<std::uint32_t>(payload.size()), 2U, sequence});
    std::vector<std::byte> message(header.begin(), header.end());
    message.reserve(header.size() + payload.size());
    for (const char character : payload)
        message.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    return message;
}

} // namespace

EncodedSessionStatus encode_replay_state(const ReplayStateMessage &state, std::uint64_t sequence) {
    const bool interrupted = state.phase == "interrupted";
    if (!identity(state.run_id) ||
        std::find(replay_phases.begin(), replay_phases.end(), state.phase) == replay_phases.end() ||
        !fits(state.take_position) || !fits(state.frames_total) ||
        (state.frame && *state.frame >= state.frames_total) ||
        interrupted != !state.interruption_cause.empty() ||
        (interrupted && !identity(state.interruption_cause)))
        return SessionStatusEncodeError::invalid_state;
    toml::table document{{"status", "replay_state"},
                         {"run_id", state.run_id},
                         {"take_position", static_cast<std::int64_t>(state.take_position)},
                         {"phase", state.phase},
                         {"frames_total", static_cast<std::int64_t>(state.frames_total)},
                         {"overlay_visible", state.overlay_visible}};
    if (state.frame)
        document.insert("frame", static_cast<std::int64_t>(*state.frame));
    if (interrupted)
        document.insert("interruption_cause", state.interruption_cause);
    return frame(document, sequence);
}

EncodedSessionStatus encode_run_opened(const RunOpenedMessage &opened, std::uint64_t sequence) {
    if (!identity(opened.run_id) || !fits(opened.take_position))
        return SessionStatusEncodeError::invalid_state;
    return frame(toml::table{{"status", "run_opened"},
                             {"run_id", opened.run_id},
                             {"take_position", static_cast<std::int64_t>(opened.take_position)},
                             {"traversal", "post_end_inspection"}},
                 sequence);
}

} // namespace ayther::audio_qa
