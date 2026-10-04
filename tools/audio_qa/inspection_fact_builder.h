#pragma once

#include "fact_model.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ayther::audio_qa {

// Spec 002, contracts.md C2 (RF-2.13, RF-5.8): the `inspection_event` fact class. The
// supervisor reads it with read_inspection_event; the Runtime builds it with the builder.
struct InspectionEvent {
    // Order of the visit, from 1.
    std::uint64_t seq{};
    // pause|resume|step_forward|step_back|recover_failed|overlay_toggle|interrupted|recovered|
    // advance_take
    std::string control;
    std::uint64_t frame_before{};
    std::uint64_t frame_after{};
    std::uint64_t visit{};
    // In the time of the take.
    std::uint64_t elapsed_ms{};
    bool operator==(const InspectionEvent &) const = default;
};

// The inspection facts have their own producer, `inspection`, with its own sequence.
inline constexpr std::string_view inspection_producer = "inspection";

[[nodiscard]] Fact make_inspection_event_fact(std::string run_id, std::uint64_t producer_sequence,
                                              const InspectionEvent &event);

// C2 (RF-7.4, RF-7.5, RF-7.7): the record of one visit to one frame. An empty reason means the
// frame was composable; a measure is written only when it is known.
struct RenderFrameRecord {
    std::uint64_t frame{};
    std::uint64_t visit{};
    std::string not_composable_reason;
    std::uint64_t occurrences{};
    std::optional<double> processing_ms;
    std::optional<double> fps_instant;
};

[[nodiscard]] Fact make_render_frame_fact(std::string run_id, std::uint64_t producer_sequence,
                                          const RenderFrameRecord &record);

} // namespace ayther::audio_qa
