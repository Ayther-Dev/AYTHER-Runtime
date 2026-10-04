#pragma once

#include "reference_model.h"
#include "replay_presentation.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::size_t max_replay_trace_facts = 10'000'000;

[[nodiscard]] inline constexpr bool
replay_trace_fact_count_within_limit(const std::uint64_t count) noexcept {
    return count <= max_replay_trace_facts;
}

struct ReplayTraceFactId {
    std::uint32_t producer{};
    std::uint64_t sequence{};
    bool operator==(const ReplayTraceFactId &) const = default;
};

struct ReplayTraceSummary {
    std::uint64_t observed_fact_count{};
    ReplayTraceFactId ingress;
    ReplayTraceFactId candidate;
    ReplayTraceFactId selection;
    ReplayTraceFactId playback_request;
    ReplayTraceFactId playback_decision;
    ReplayTraceFactId playback_effect;
    ReplayTraceFactId mix_span;
    std::uint64_t occurrence{};
    bool causally_connected{};
    bool loss_free{};
    bool operator==(const ReplayTraceSummary &) const = default;
};

struct ReplayExecutionResult {
    std::string run_id;
    std::string take_id;
    std::uint32_t recording_frames{};
    std::uint32_t inputs_consumed{};
    std::uint64_t assignment_count{};
    ContentIdentity initial_game_state;
    ContentIdentity final_game_state;
    ReplayTraceSummary trace;
    bool succeeded{};
    std::string code;
    ReplayPresentation presentation;
    // Terminal 1.4 (spec 002, contracts.md C1-5): playback, traversal and evidence are
    // separate results (RF-2.4). A 1.2 or 1.3 terminal has neither traversal nor playback:
    // they are unknown, never assumed linear (contracts.md C2).
    std::optional<std::string> traversal;
    std::optional<std::string> playback;
    bool linear_completed{};
    std::vector<std::string> evidence_reasons;
    bool ended_paused{};
    std::uint64_t user_pause_ms{};
    std::uint32_t interruptions{};
    bool operator==(const ReplayExecutionResult &) const = default;
};

inline constexpr std::size_t max_terminal_evidence_reasons = 64;

// Spec 002 (contracts.md C1-5): the terminal fields the Runtime derives from its own
// outcome before sending it. Without inspection every traversal is linear; all inputs
// consumed without cancellation is a natural end, even when the evidence is incomplete.
// Evidence reasons already present are kept and a failure adds its code.
void describe_linear_terminal(ReplayExecutionResult &result, bool cancelled);

enum class ReplayExecutionResultError {
    invalid_model,
    header_rejected,
    wrong_message_type,
    sequence_mismatch,
    length_mismatch,
    invalid_payload,
};

using EncodedReplayExecutionResult =
    std::variant<std::vector<std::byte>, ReplayExecutionResultError>;
using DecodedReplayExecutionResult =
    std::variant<ReplayExecutionResult, ReplayExecutionResultError>;

[[nodiscard]] bool well_formed(const ReplayExecutionResult &result) noexcept;
[[nodiscard]] EncodedReplayExecutionResult
encode_replay_execution_result(const ReplayExecutionResult &result, std::uint64_t sequence);
[[nodiscard]] DecodedReplayExecutionResult
decode_replay_execution_result(std::span<const std::byte> message, std::uint64_t expected_sequence);

} // namespace ayther::audio_qa
