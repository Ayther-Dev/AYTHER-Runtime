#include "request_outcome.h"

#include <algorithm>

namespace ayther::audio_qa {

std::string_view playback_kind_code(PlaybackKind kind) noexcept {
    switch (kind) {
    case PlaybackKind::natural_end:
        return "natural_end";
    case PlaybackKind::cancelled:
        return "cancelled";
    case PlaybackKind::failed:
        return "failed";
    case PlaybackKind::interrupted:
        return "interrupted";
    }
    return "failed";
}

std::optional<PlaybackKind> parse_playback_kind(std::string_view code) noexcept {
    for (const auto kind : {PlaybackKind::natural_end, PlaybackKind::cancelled,
                            PlaybackKind::failed, PlaybackKind::interrupted})
        if (playback_kind_code(kind) == code)
            return kind;
    return std::nullopt;
}

std::string_view traversal_kind_code(TraversalKind kind) noexcept {
    switch (kind) {
    case TraversalKind::linear:
        return "linear";
    case TraversalKind::inspection:
        return "inspection";
    case TraversalKind::post_end_inspection:
        return "post_end_inspection";
    }
    return "linear";
}

std::optional<TraversalKind> parse_traversal_kind(std::string_view code) noexcept {
    for (const auto kind :
         {TraversalKind::linear, TraversalKind::inspection, TraversalKind::post_end_inspection})
        if (traversal_kind_code(kind) == code)
            return kind;
    return std::nullopt;
}

std::string_view request_phase_code(RequestPhaseKind kind) noexcept {
    switch (kind) {
    case RequestPhaseKind::validating:
        return "validating";
    case RequestPhaseKind::admitted:
        return "admitted";
    case RequestPhaseKind::preparing:
        return "preparing";
    case RequestPhaseKind::running:
        return "running";
    case RequestPhaseKind::closing:
        return "closing";
    case RequestPhaseKind::closed:
        return "closed";
    }
    return "validating";
}

bool linear_complete(const std::vector<TakeSlotOutcome> &per_take) noexcept {
    return !per_take.empty() &&
           std::all_of(per_take.begin(), per_take.end(), [](const TakeSlotOutcome &slot) {
               const auto *take = std::get_if<TakeOutcome>(&slot);
               return take != nullptr && take->traversal == TraversalKind::linear &&
                      take->playback.kind == PlaybackKind::natural_end && take->evidence.complete &&
                      take->linear_completed;
           });
}

} // namespace ayther::audio_qa
