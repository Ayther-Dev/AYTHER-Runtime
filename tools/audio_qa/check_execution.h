#pragma once

#include "audio_chunk.h"
#include "check_options.h"
#include "fact_model.h"
#include "integrated_evidence.h"
#include "model.h"
#include "replay_execution_result.h"

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

[[nodiscard]] CheckExecutionResult execute_check_replay(const CheckOptions &options,
                                                        const Request &request,
                                                        std::string run_id) noexcept;
[[nodiscard]] std::string_view check_execution_error_code(CheckExecutionError error) noexcept;

} // namespace ayther::audio_qa
