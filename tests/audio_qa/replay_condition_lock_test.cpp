#include "replay_condition_lock.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        const qa::ReplayConditions initial{"arcade", 0x5U, 60000U, 1001U};
        const qa::ReplayConditionLock lock{initial};

        auto proposed = initial;
        proposed.profile_id = "cinematic";
        auto result = lock.check_change(proposed, qa::ReplayConditionChangeSource::configuration);
        require(result.disposition == qa::ReplayConditionDisposition::rejected &&
                    result.source == qa::ReplayConditionChangeSource::configuration &&
                    result.differing_fields == qa::condition_profile &&
                    result.code == "configuration_change_rejected" && lock.conditions() == initial,
                "profile_change_was_not_rejected_or_changed_active_conditions");

        proposed = initial;
        proposed.muted_audio_buses = 0x2U;
        proposed.speed_numerator = 4U;
        proposed.speed_denominator = 1U;
        result = lock.check_change(proposed, qa::ReplayConditionChangeSource::external_reload);
        require(result.disposition == qa::ReplayConditionDisposition::rejected &&
                    result.source == qa::ReplayConditionChangeSource::external_reload &&
                    result.differing_fields == (qa::condition_mute | qa::condition_speed) &&
                    result.code == "external_reload_rejected" && lock.conditions() == initial,
                "reload_changed_mute_or_speed_silently");

        result = lock.check_change(initial, qa::ReplayConditionChangeSource::external_reload);
        require(result.disposition == qa::ReplayConditionDisposition::unchanged &&
                    result.differing_fields == qa::condition_none &&
                    result.code == "conditions_unchanged" && lock.conditions() == initial,
                "equivalent_reload_was_reported_as_a_change");

        std::puts("replay_condition_lock_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "replay_condition_lock_test: %s\n", error.what());
        return 1;
    }
}
