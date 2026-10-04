// Spec 002, BR-134 (RF-3.6; contracts.md C3): the types of the render observation come from
// the Engine package of G3, and the views borrowed during the callback are copied into an
// owned observation that outlives the producer storage.
#include "render_observation.h"

#include <ayther/engine/render_observer.hpp>

#include <array>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

namespace ri = ayther::replay_inspection;
namespace ro = ayther::engine::render_observation;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

ri::render::RenderObservation observe_and_forget() {
    // Producer storage that dies with this scope.
    std::string pose{"chicken_leg_walk_3"};
    std::string kind{"pose"};
    std::string asset{"sprites/chicken_leg/walk_3.png"};
    std::string reason{"texture_pending"};
    const std::array<ro::OccurrenceId, 2> members{{{0, 4, 0}, {1, 5, 1}}};
    std::array<ro::OccurrenceView, 3> occurrences{};
    occurrences[0].id = {0, 4, 0};
    occurrences[0].identity_hash = 0xABCDU;
    occurrences[0].status = ro::OccurrenceStatus::replaced;
    occurrences[0].replacement = 0;
    occurrences[0].pose = {"pose", ro::Availability::known, {}, std::string_view{pose}, {}};
    occurrences[0].not_applied_reason = {
        "not_applied_reason", ro::Availability::not_applicable, {}, {}, "replaced"};
    occurrences[1].id = {1, 5, 1};
    occurrences[1].status = ro::OccurrenceStatus::replaced;
    occurrences[1].replacement = 0;
    occurrences[2].id = {2, 9, 0};
    occurrences[2].status = ro::OccurrenceStatus::assigned_not_applied;
    occurrences[2].not_applied_reason = {
        "not_applied_reason", ro::Availability::known, {}, std::string_view{reason}, {}};
    std::array<ro::ReplacementView, 1> replacements{};
    replacements[0].index = 0;
    replacements[0].kind = kind;
    replacements[0].pose_key = pose;
    replacements[0].asset = asset;
    replacements[0].members = members;
    replacements[0].render_availability = ro::Availability::known;
    replacements[0].draw = ro::DrawOutcome::partitioned;
    replacements[0].texture = ro::TextureState::ready;
    ro::RenderFrameView view;
    view.frame = {ro::Availability::known, 719, {}};
    view.composability = ro::Composability::composable;
    view.occurrences = occurrences;
    view.replacements = replacements;
    view.occurrences_total = 300;
    view.replacements_total = 1;
    auto copy = ri::render::copy_observation(view);
    pose.assign(pose.size(), 'x');
    asset.assign(asset.size(), 'x');
    reason.assign(reason.size(), 'x');
    return copy;
}

} // namespace

int main() {
    static_assert(std::is_same_v<ri::render::OccurrenceStatus, ro::OccurrenceStatus> &&
                      std::is_same_v<ri::render::Composability, ro::Composability> &&
                      std::is_same_v<ri::render::DrawOutcome, ro::DrawOutcome> &&
                      std::is_same_v<ri::render::OccurrenceId, ro::OccurrenceId> &&
                      std::is_same_v<ri::render::Availability, ro::Availability>,
                  "RF-3.6: the C3 types are those of the Engine package");
    expect(ro::supports(ro::contract_version), "C3: the package declares render observation 1.0");
    const auto copy = observe_and_forget();
    expect(copy.emulation_frame == 719U && copy.occurrences.size() == 3U &&
               copy.occurrences_total == 300U && copy.replacements_total == 1U,
           "C3: the frame and the totals are copied, an excess is kept");
    expect(copy.occurrences[0].pose.availability == ri::render::Availability::known &&
               copy.occurrences[0].pose.value == "chicken_leg_walk_3" &&
               copy.occurrences[0].not_applied_reason.availability ==
                   ri::render::Availability::not_applicable,
           "C3: a known pose survives its producer storage");
    expect(copy.occurrences[2].not_applied_reason.value == "texture_pending" &&
               copy.occurrences[1].pose.availability == ri::render::Availability::unknown,
           "C3: the reason is copied and an absent pose stays unknown");
    expect(copy.replacements.size() == 1U &&
               copy.replacements[0].asset == "sprites/chicken_leg/walk_3.png" &&
               copy.replacements[0].members == std::vector<std::uint16_t>{0, 1} &&
               copy.replacements[0].draw == ri::render::DrawOutcome::partitioned &&
               copy.replacements[0].texture == ri::render::TextureState::ready,
           "C3: the replacement keeps its exact members and draw");
    ro::RenderFrameView unknown_frame;
    expect(!ri::render::copy_observation(unknown_frame).frame_known,
           "C3: an unknown frame position is not invented");
    if (failures != 0)
        return 1;
    std::cout << "the render observation is copied from the Engine package\n";
    return 0;
}
