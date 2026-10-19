#pragma once

#include "capabilities.h"
#include "fact_model.h"
#include "field_issue.h"
#include "inspection_fact_builder.h"
#include "request_outcome.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace ayther::audio_qa {

// Spec 002 (contracts.md C1): the supervisor side of the Runtime QA protocol 1.1.
inline constexpr ContractVersion runtime_protocol_v11{1, 1};

// Visible replay needs 1.1 with `inspection_v1` and `visible_replay_v1`; the missing one
// is named in the --runtime field. Without presentation 1.1 is chosen when offered and a
// Runtime that only offers 1.0 keeps working (RNF-6).
using ProtocolChoice = std::variant<ContractVersion, FieldIssue>;
[[nodiscard]] ProtocolChoice negotiate_runtime_protocol(const CapabilitySet &offer,
                                                        std::string_view presentation);
// Spec 006 (RF-4.4): a request with pad timelines also needs `timeline_replay_v1`
// (`runtime_missing_timeline_replay_v1` otherwise); a request without them negotiates as
// before, so a Runtime that predates timelines keeps serving takes.
inline constexpr std::string_view timeline_replay_capability = "timeline_replay_v1";
[[nodiscard]] ProtocolChoice negotiate_runtime_protocol(const CapabilitySet &offer,
                                                        std::string_view presentation,
                                                        bool timelines);
// Spec 006 (RF-7.3): everything a request may need from the Runtime beyond protocol 1.1.
// A window that closes itself after the natural end needs `close_at_end_v1`
// (`runtime_missing_close_at_end_v1` otherwise); a request that keeps the inspection
// negotiates as before.
struct ProtocolNeeds {
    std::string_view presentation{"none"};
    bool timelines{};
    bool close_at_end{};
};
inline constexpr std::string_view close_at_end_capability = "close_at_end_v1";
[[nodiscard]] ProtocolChoice negotiate_runtime_protocol(const CapabilitySet &offer,
                                                        const ProtocolNeeds &needs);

// C1-6: the Runtime opened a post-end inspection traversal with its own run.
struct RunOpened {
    std::string run_id;
    std::size_t take_position{};
    bool operator==(const RunOpened &) const = default;
};

enum class ProtocolV11Error { header_rejected, invalid_payload };

using SessionStatus = std::variant<ReplayStateView, RunOpened, ProtocolV11Error>;

// Decodes a complete `session_status` message (type 4, data channel) with its sequence.
[[nodiscard]] SessionStatus decode_session_status(std::span<const std::byte> message,
                                                  std::uint64_t expected_sequence);

// C2: the `inspection_event` fact class (InspectionEvent, inspection_fact_builder.h).
[[nodiscard]] std::optional<InspectionEvent> read_inspection_event(const Fact &fact);

} // namespace ayther::audio_qa
