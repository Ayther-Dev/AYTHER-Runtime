#include "render_observation.h"

#include <string_view>
#include <utility>
#include <variant>

namespace ayther::replay_inspection::render {
namespace {

TextField copy_text(const engine::audio_observation::FieldView &field) {
    TextField text;
    text.availability = field.availability;
    if (field.availability != Availability::known)
        return text;
    if (const auto *value = std::get_if<std::string_view>(&field.value))
        text.value = std::string{*value};
    else
        // C3: a known text field without text is not trusted as known.
        text.availability = Availability::unknown;
    return text;
}

} // namespace

RenderObservation copy_observation(const engine_render::RenderFrameView &view) {
    RenderObservation observation;
    observation.frame_known = view.frame.availability == Availability::known;
    observation.emulation_frame = observation.frame_known ? view.frame.emulation_frame : 0U;
    observation.composability = view.composability;
    observation.occurrences_total = view.occurrences_total;
    observation.replacements_total = view.replacements_total;
    observation.occurrences.reserve(view.occurrences.size());
    for (const auto &occurrence : view.occurrences)
        observation.occurrences.push_back(
            {occurrence.id, occurrence.identity_hash, occurrence.status, occurrence.replacement,
             copy_text(occurrence.pose), copy_text(occurrence.not_applied_reason)});
    observation.replacements.reserve(view.replacements.size());
    for (const auto &replacement : view.replacements) {
        ObservedReplacement copy;
        copy.index = replacement.index;
        copy.kind = std::string{replacement.kind};
        copy.pose_key = std::string{replacement.pose_key};
        copy.asset = std::string{replacement.asset};
        copy.members.reserve(replacement.members.size());
        for (const auto &member : replacement.members)
            copy.members.push_back(member.index);
        copy.render_availability = replacement.render_availability;
        copy.draw = replacement.draw;
        copy.texture = replacement.texture;
        observation.replacements.push_back(std::move(copy));
    }
    return observation;
}

} // namespace ayther::replay_inspection::render
