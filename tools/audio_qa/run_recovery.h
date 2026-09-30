#pragma once

#include "checkpoint_store.h"
#include "request_ledger.h"

#include <filesystem>
#include <optional>
#include <string_view>
#include <variant>

namespace ayther::audio_qa {

enum class RecoveredCheckpointState { absent, verified, unreadable };

struct InterruptedRunRecovery {
    Run run;
    RecoveredCheckpointState checkpoint_state{RecoveredCheckpointState::absent};
    std::optional<EvidenceCheckpoint> checkpoint;
    bool automatic_resume_allowed{};
};

enum class RunRecoveryError {
    request_not_found,
    run_already_closed,
    ledger_update_failed,
    io_error,
};

using RunRecoveryResult = std::variant<InterruptedRunRecovery, RunRecoveryError>;

[[nodiscard]] RunRecoveryResult
recover_interrupted_run(RequestLedger &ledger, std::string_view request_id,
                        const std::filesystem::path &checkpoint_path) noexcept;

} // namespace ayther::audio_qa
