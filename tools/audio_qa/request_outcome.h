#pragma once

#include "field_issue.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

// Spec 002, plan §4.2 (RF-2.3, RF-2.4, RF-2.12, RF-2.13, RF-5.8): how a request and each
// of its takes end. Playback, traversal and evidence are separate results: a technical
// natural end is never a visual evaluation (RF-2.7).
enum class PlaybackKind { natural_end, cancelled, failed, interrupted };

struct PlaybackOutcome {
    PlaybackKind kind{PlaybackKind::failed};
    // Empty for a natural end or a cancellation.
    std::string diagnostic;
    bool operator==(const PlaybackOutcome &) const = default;
};

enum class TraversalKind { linear, inspection, post_end_inspection };

struct EvidenceOutcome {
    bool complete{};
    // Why the evidence is incomplete; empty when it is complete.
    std::vector<std::string> reasons;
    bool operator==(const EvidenceOutcome &) const = default;
};

struct TakeOutcome {
    std::size_t position{};
    std::string take;
    std::string run_id;
    PlaybackOutcome playback;
    // Unknown when the Runtime did not report it (terminal 1.3 or older, contracts.md C2):
    // never assumed linear.
    std::optional<TraversalKind> traversal;
    EvidenceOutcome evidence;
    // Every input consumed in a linear traversal that ended naturally.
    bool linear_completed{};
    bool operator==(const TakeOutcome &) const = default;
};

struct NotStartedTake {
    std::size_t position{};
    std::string take;
    // not_started_after_cancellation | not_started_after_failure
    std::string reason;
    bool operator==(const NotStartedTake &) const = default;
};

using TakeSlotOutcome = std::variant<TakeOutcome, NotStartedTake>;

struct RequestOutcome {
    std::vector<TakeSlotOutcome> per_take;
    bool confirmed{};
    // Additive (spec 002, CLI and launcher): the identity of the request, the exit code of
    // the CLI (0 complete, 2 incomplete, 3 invalid, 4 evidence not preserved), the issues
    // that kept it from being admitted, whether it was returned from its confirmed summary
    // without running, and the joint result of RF-2.12.
    std::string request_id;
    int exit_code{3};
    std::vector<FieldIssue> issues;
    bool known{};
    bool linear_complete{};
    bool operator==(const RequestOutcome &) const = default;
};

// Contracts.md C1-3: the live state of the active take for the launcher; never evidence.
struct ReplayStateView {
    std::string run_id;
    std::size_t take_position{};
    std::string phase;
    std::optional<std::uint64_t> frame;
    std::uint64_t frames_total{};
    bool overlay_visible{};
    std::string interruption_cause;
    bool operator==(const ReplayStateView &) const = default;
};

enum class RequestPhaseKind { validating, admitted, preparing, running, closing, closed };

struct RequestPhase {
    RequestPhaseKind kind{RequestPhaseKind::validating};
    // The take of `preparing` and `running`.
    std::size_t take_position{};
    bool operator==(const RequestPhase &) const = default;
};

[[nodiscard]] std::string_view playback_kind_code(PlaybackKind kind) noexcept;
[[nodiscard]] std::optional<PlaybackKind> parse_playback_kind(std::string_view code) noexcept;
[[nodiscard]] std::string_view traversal_kind_code(TraversalKind kind) noexcept;
[[nodiscard]] std::optional<TraversalKind> parse_traversal_kind(std::string_view code) noexcept;
[[nodiscard]] std::string_view request_phase_code(RequestPhaseKind kind) noexcept;

// RF-2.12: the request reproduced every take linearly and completely only when each one
// is linear, ended naturally and kept complete evidence. A take that did not start, was
// inspected, cancelled or failed, or whose traversal is unknown, prevents it.
[[nodiscard]] bool linear_complete(const std::vector<TakeSlotOutcome> &per_take) noexcept;

} // namespace ayther::audio_qa
