#include "check_profile.h"

#include <string>
#include <variant>

namespace qa = ayther::audio_qa;

namespace {

qa::CheckOptions base_options() {
    qa::CheckOptions options;
    options.runtime = "runtime.exe";
    options.reference = "reference.toml";
    options.play_manifest = "play.toml";
    options.pack = "pack.ay";
    options.output = "evidence";
    return options;
}

bool default_selects_only_primary() {
    const qa::CheckProfile profile{"profile", "main.ayr", "missing-extra.arp"};
    const auto result = qa::select_check_takes(base_options(), profile);
    const auto *selection = std::get_if<qa::TakeSelection>(&result);
    return selection != nullptr && selection->source == qa::TakeSelectionSource::profile_primary &&
           selection->takes == std::vector<std::string>{"main.ayr"};
}

bool explicit_selection_does_not_add_profile_takes() {
    auto options = base_options();
    options.takes = {"chosen.arp"};
    const qa::CheckProfile profile{"profile", "main.ayr", "extra.arp"};
    const auto result = qa::select_check_takes(options, profile);
    const auto *selection = std::get_if<qa::TakeSelection>(&result);
    return selection != nullptr && selection->source == qa::TakeSelectionSource::explicit_options &&
           selection->takes == std::vector<std::string>{"chosen.arp"};
}

bool complementary_requires_explicit_selection() {
    auto options = base_options();
    const qa::CheckProfile profile{"profile", "main.ayr", "extra.arp"};
    const auto default_result = qa::select_check_takes(options, profile);
    const auto *default_selection = std::get_if<qa::TakeSelection>(&default_result);
    if (default_selection == nullptr ||
        default_selection->takes != std::vector<std::string>{"main.ayr"})
        return false;

    options.takes = {profile.complementary_take};
    const auto explicit_result = qa::select_check_takes(options, profile);
    const auto *explicit_selection = std::get_if<qa::TakeSelection>(&explicit_result);
    return explicit_selection != nullptr &&
           explicit_selection->takes == std::vector<std::string>{"extra.arp"};
}

bool missing_primary_is_rejected_only_for_default() {
    auto options = base_options();
    const qa::CheckProfile profile{"profile", "", "extra.arp"};
    if (!std::holds_alternative<qa::TakeSelectionError>(qa::select_check_takes(options, profile)))
        return false;
    options.takes = {"chosen.ayr"};
    return std::holds_alternative<qa::TakeSelection>(qa::select_check_takes(options, profile));
}

} // namespace

int main() {
    const auto profile = qa::golden_axe_check_profile();
    const bool named_profile = profile.profile_id == "golden-axe-world-rev-a" &&
                               profile.primary_take.ends_with("Toma 3.ayr") &&
                               profile.complementary_take.ends_with("Demo Amazona.arp");
    return named_profile && default_selects_only_primary() &&
                   explicit_selection_does_not_add_profile_takes() &&
                   complementary_requires_explicit_selection() &&
                   missing_primary_is_rejected_only_for_default()
               ? 0
               : 1;
}
