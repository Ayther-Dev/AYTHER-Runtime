#pragma once

#include <string_view>

namespace runtime {

struct MusicRestoreInput {
    bool game_valid{};
    bool core_compatible{};
    bool pack_required{};
    bool pack_valid{};
    bool hd_present{};
    bool hd_complete{};
    bool hd_compatible{};
    bool apply_failed{};
    bool session_active{};
};

enum class MusicRestoreOutcome { prepared, fresh_hd, rejected, rollback };

struct MusicRestoreClassificationResult {
    MusicRestoreOutcome outcome{MusicRestoreOutcome::rejected};
    bool original_audio{};
    bool session_preserved{};
    bool rollback_required{};
    std::string_view diagnostic;
};

class MusicRestoreClassification {
  public:
    [[nodiscard]] MusicRestoreClassificationResult
    classify(const MusicRestoreInput &input) const noexcept {
        if (!input.game_valid || !input.core_compatible)
            return {MusicRestoreOutcome::rejected, false, input.session_active, false,
                    "restore_game_incompatible"};
        if (input.pack_required && !input.pack_valid)
            return {MusicRestoreOutcome::rejected, false, input.session_active, false,
                    "restore_pack_invalid"};
        if (!input.hd_present || !input.hd_complete || !input.hd_compatible)
            return {MusicRestoreOutcome::fresh_hd, true, input.session_active, false,
                    "restore_hd_fresh"};
        if (input.apply_failed)
            return {MusicRestoreOutcome::rollback, false, input.session_active, true,
                    "restore_apply_failed"};
        return {MusicRestoreOutcome::prepared, false, input.session_active, false, {}};
    }
};

} // namespace runtime
