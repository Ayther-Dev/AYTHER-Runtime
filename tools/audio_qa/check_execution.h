#pragma once

#include "audio_chunk.h"
#include "cancel_token.h"
#include "check_options.h"
#include "effective_values.h"
#include "fact_model.h"
#include "integrated_evidence.h"
#include "model.h"
#include "replay_execution_result.h"
#include "runtime_protocol_v11.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

enum class CheckExecutionError {
    reference_unavailable,
    manifest_unavailable,
    manifest_missing_material,
    runtime_identity_unavailable,
    runtime_incompatible,
    runtime_data_isolation_failed,
    user_data_changed,
    channel_unavailable,
    request_encoding_failed,
    runtime_launch_failed,
    request_delivery_failed,
    admission_failed,
    terminal_failed,
    runtime_timeout,
    runtime_failed,
    result_mismatch,
    result_identity_mismatch,
    result_input_mismatch,
    result_evidence_mismatch,
    result_exit_mismatch,
    evidence_stream_invalid,
};

struct CheckExecutionEvidence {
    ReplayExecutionResult replay;
    std::optional<IntegratedEvidenceSummary> preserved;
    std::optional<IntegratedEvidenceError> preservation_error;
    bool runtime_data_isolated{};
};

using CheckExecutionResult = std::variant<CheckExecutionEvidence, CheckExecutionError>;

// Spec 002: what the supervisor gives a running take. The cancellation is forwarded as
// `cancel` over the control channel, which stays open until the terminal (contracts.md
// C1, RF-2.5); `on_running` is called once the Runtime admitted the take (RF-2.3).
struct ReplayExecutionControl {
    const CancelToken *cancel{};
    std::function<void()> on_running;
    // C1-3: the live state the Runtime reports in `session_status`; never evidence.
    std::function<void(const ReplayStateView &)> on_replay_state;
    // C1-3, plan §5.11: the position of the take and whether it is the last one.
    std::size_t take_position{};
    bool last_take{};
};

// RNF-5: the cessation of a take is confirmed only after its process exited and its
// evidence was closed; a take whose process never started has nothing to cease.
struct ReplayCessation {
    bool launched{};
    bool exited{};
    bool evidence_closed{};
    [[nodiscard]] bool confirmed() const noexcept {
        return !launched || (exited && evidence_closed);
    }
};

// Spec 002 (RF-1.3, RF-1.5, RF-1.6): the Runtime command line comes only from the
// effective request. No pack means no `--pack`; each condition is passed only when
// some source gives it.
// BR-135 (contracts.md C1-3, plan §5.11): the position of the take in the request and whether
// it is the last one.
[[nodiscard]] std::vector<std::wstring>
runtime_replay_arguments(const EffectiveRequest &request, std::string_view control_token,
                         std::string_view data_token, std::string_view run_id,
                         std::size_t take_position = 0U, bool last_take = false);

[[nodiscard]] CheckExecutionResult execute_check_replay(const EffectiveRequest &effective,
                                                        const Request &request, std::string run_id,
                                                        const ReplayExecutionControl &control,
                                                        ReplayCessation &cessation) noexcept;
[[nodiscard]] std::string_view check_execution_error_code(CheckExecutionError error) noexcept;

} // namespace ayther::audio_qa
