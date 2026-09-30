#include "replay_condition_lock.h"

#include <utility>

namespace ayther::audio_qa {

ReplayConditionLock::ReplayConditionLock(ReplayConditions conditions) noexcept
    : conditions_(std::move(conditions)) {}

const ReplayConditions &ReplayConditionLock::conditions() const noexcept { return conditions_; }

ReplayConditionChangeResult
ReplayConditionLock::check_change(const ReplayConditions &proposed,
                                  const ReplayConditionChangeSource source) const noexcept {
    std::uint32_t fields = condition_none;
    if (proposed.profile_id != conditions_.profile_id) {
        fields |= condition_profile;
    }
    if (proposed.muted_audio_buses != conditions_.muted_audio_buses) {
        fields |= condition_mute;
    }
    if (proposed.speed_numerator != conditions_.speed_numerator ||
        proposed.speed_denominator != conditions_.speed_denominator) {
        fields |= condition_speed;
    }
    if (fields == condition_none) {
        return {ReplayConditionDisposition::unchanged, source, fields, "conditions_unchanged"};
    }
    return {ReplayConditionDisposition::rejected, source, fields,
            source == ReplayConditionChangeSource::external_reload
                ? "external_reload_rejected"
                : "configuration_change_rejected"};
}

} // namespace ayther::audio_qa
