#pragma once

#include "cancel_token.h"
#include "check_messages.h"
#include "check_option_descriptors.h"
#include "check_options.h"
#include "effective_values.h"
#include "field_issue.h"
#include "request_outcome.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ayther::audio_qa {

// Spec 002 (contracts.md C5, «Biblioteca run_check 1.0»): the request library consumed by
// the CLI and by the launcher. The request has the selections and options of the CLI
// (plan §4.1); the options are described by the single table of `check`.
using ReplayRequest = CheckOptions;
using OptionDescriptor = CheckOptionDescriptor;

// Validation before admission: effective values with their origin and every issue, in
// its field. Nothing is written.
struct PreflightResult {
    std::vector<EffectiveValue> effective;
    std::vector<FieldIssue> issues;
    [[nodiscard]] bool startable() const noexcept { return issues.empty(); }
};

// Called on the thread of run_check. A take outcome is reported only once confirmed.
class CheckObserver {
  public:
    CheckObserver() = default;
    CheckObserver(const CheckObserver &) = delete;
    CheckObserver &operator=(const CheckObserver &) = delete;
    virtual ~CheckObserver() = default;

    virtual void on_phase(RequestPhase phase) = 0;
    virtual void on_replay_state(const ReplayStateView &state) = 0;
    virtual void on_take_outcome(const TakeOutcome &outcome) = 0;
    // Additive (spec 002): the diagnostic lines of the CLI (`audio_qa_effective`,
    // `audio_qa_status`, `audio_qa_error`, `audio_qa_replay`, `audio_qa_summary`) and the
    // localized messages. The launcher may ignore them.
    virtual void on_report(std::string_view line);
    virtual void on_message(CheckMessage message);
};

[[nodiscard]] std::span<const OptionDescriptor> option_descriptors() noexcept;
[[nodiscard]] PreflightResult preflight(const ReplayRequest &request);

// Validates (repeating the preflight), admits, runs the takes in order and confirms the
// result. Only one request runs per destination; a repeated request id returns its
// confirmed summary without running (`known`) or is rejected when its selections differ.
[[nodiscard]] RequestOutcome run_check(const ReplayRequest &request, CheckObserver &observer,
                                       CancelToken &cancel);

} // namespace ayther::audio_qa
