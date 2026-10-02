#include "run_recovery.h"

#include <filesystem>
#include <system_error>
#include <utility>
#include <variant>

namespace ayther::audio_qa {

RunRecoveryResult recover_interrupted_run(RequestLedger &ledger, const std::string_view request_id,
                                          const std::filesystem::path &checkpoint_path) noexcept {
    try {
        const auto stored_run = ledger.find_run(request_id);
        if (!stored_run) {
            return RunRecoveryError::request_not_found;
        }
        if (stored_run->phase == Phase::closed) {
            return RunRecoveryError::run_already_closed;
        }

        RecoveredCheckpointState checkpoint_state = RecoveredCheckpointState::absent;
        std::optional<EvidenceCheckpoint> checkpoint;
        std::error_code error;
        const bool checkpoint_exists = std::filesystem::exists(checkpoint_path, error);
        if (error) {
            return RunRecoveryError::io_error;
        }
        if (checkpoint_exists) {
            const auto opened = read_checkpoint(checkpoint_path);
            const auto *stored = std::get_if<StoredCheckpoint>(&opened);
            if (stored == nullptr) {
                checkpoint_state = RecoveredCheckpointState::unreadable;
            } else {
                checkpoint_state = RecoveredCheckpointState::verified;
                checkpoint = stored->checkpoint();
            }
        }

        auto interrupted = *stored_run;
        interrupted.evidence_result = EvidenceResult::incomplete;
        const auto updated = ledger.update_run(interrupted);
        const auto *updated_value = std::get_if<bool>(&updated);
        if (updated_value == nullptr || !*updated_value) {
            return RunRecoveryError::ledger_update_failed;
        }
        return InterruptedRunRecovery{std::move(interrupted), checkpoint_state,
                                      std::move(checkpoint), false};
    } catch (...) {
        return RunRecoveryError::io_error;
    }
}

} // namespace ayther::audio_qa
