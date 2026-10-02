#include "check_profile.h"

namespace ayther::audio_qa {

CheckProfile golden_axe_check_profile() {
    return {
        "golden-axe-world-rev-a",
        "C:/Users/david/Documents/AetherProjects/Golden Axe (World) (Rev A)/"
        "recordings/Toma 3.ayr",
        "C:/Users/david/Documents/AetherProjects/Golden Axe (World) (Rev A)/"
        "recordings/Demo Amazona.arp",
    };
}

TakeSelectionResult select_check_takes(const CheckOptions &options, const CheckProfile &profile) {
    if (!options.takes.empty())
        return TakeSelection{TakeSelectionSource::explicit_options, options.takes};
    if (profile.primary_take.empty())
        return TakeSelectionError::missing_profile_primary_take;
    return TakeSelection{TakeSelectionSource::profile_primary, {profile.primary_take}};
}

} // namespace ayther::audio_qa
