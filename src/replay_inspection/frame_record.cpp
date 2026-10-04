#include "frame_record.h"

#include <algorithm>
#include <utility>

namespace ayther::replay_inspection {
namespace {

std::optional<std::string> known(const render::TextField &field) {
    if (field.availability != render::Availability::known)
        return std::nullopt;
    return field.value;
}

} // namespace

FrameRecord make_frame_record(std::uint32_t frame, std::uint32_t visit, FrameGeneral general,
                              const render::RenderObservation &observation,
                              Measurements measurements) {
    FrameRecord record;
    record.frame = frame;
    record.visit = visit;
    record.general = std::move(general);
    record.measurements = measurements;
    if (observation.composability != render::Composability::composable)
        record.not_composable = observation.composability;
    const auto total = std::max(observation.occurrences_total, observation.occurrences.size());
    const auto kept = std::min(observation.occurrences.size(), max_record_rows);
    if (total > max_record_rows)
        record.overflow = RowOverflow{total, max_record_rows};
    record.rows.reserve(kept);
    for (std::size_t index = 0; index < kept; ++index) {
        const auto &occurrence = observation.occurrences[index];
        OccurrenceRow row;
        row.index = occurrence.id.index;
        row.slot = occurrence.id.slot;
        row.chain = occurrence.id.chain;
        row.identity = occurrence.identity_hash;
        row.status = occurrence.status;
        row.pose = known(occurrence.pose);
        // RF-7.3: the replacement of this same frame, never an accumulated count.
        if (occurrence.status == render::OccurrenceStatus::replaced &&
            occurrence.replacement >= 0 &&
            static_cast<std::size_t>(occurrence.replacement) < observation.replacements.size()) {
            const auto &replacement =
                observation.replacements[static_cast<std::size_t>(occurrence.replacement)];
            row.asset = replacement.asset;
            if (!row.pose && !replacement.pose_key.empty())
                row.pose = replacement.pose_key;
            if (replacement.render_availability == render::Availability::known)
                row.draw = replacement.draw;
        }
        // RF-7.9: a reason only when the Engine knows it.
        if (occurrence.status == render::OccurrenceStatus::assigned_not_applied)
            row.reason = known(occurrence.not_applied_reason);
        record.rows.push_back(std::move(row));
    }
    return record;
}

Measurements FrameMeasurer::measure(const FrameTiming &timing, FrameContext context) {
    Measurements result;
    if (timing.composed_ms >= timing.input_started_ms)
        result.processing_ms = timing.composed_ms - timing.input_started_ms;
    const bool consecutive = context == FrameContext::continuous && previous_ &&
                             previous_->frame + 1U == timing.frame &&
                             timing.presented_ms > previous_->presented_ms;
    if (consecutive)
        result.fps_instant = 1000.0 / (timing.presented_ms - previous_->presented_ms);
    // A navigation visit is not part of continuous playback.
    if (context == FrameContext::navigation)
        previous_.reset();
    else
        previous_ = timing;
    return result;
}

} // namespace ayther::replay_inspection
