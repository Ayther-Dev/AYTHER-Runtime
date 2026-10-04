#pragma once

#include "render_observation.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ayther::replay_inspection {

// Spec 002, plan §4.3 and §5.9 (RF-7, RNF-3): the immutable debug record of one visit to
// one frame, copied from the Engine observation as soon as the frame is produced. The
// overlay only draws records.
inline constexpr std::size_t max_record_rows = 256; // §8 P-11

struct FrameGeneral {
    std::string rom;
    std::string take;
    std::optional<std::string> pack;
    std::string phase;
    std::uint32_t total_frames{};
    // RF-1.3: no pack is «Sin pack».
    [[nodiscard]] std::string pack_label() const { return pack.value_or("Sin pack"); }
};

struct OccurrenceRow {
    std::uint16_t index{};
    std::uint8_t slot{};
    std::uint8_t chain{};
    std::uint64_t identity{};
    render::OccurrenceStatus status{render::OccurrenceStatus::original_unassigned};
    // Present only when the Engine marked them `known` (RF-7.7).
    std::optional<std::string> pose;
    std::optional<std::string> asset;
    std::optional<render::DrawOutcome> draw;
    std::optional<std::string> reason;
};

struct Measurements {
    std::optional<double> processing_ms;
    std::optional<double> fps_instant;
};

struct RowOverflow {
    std::size_t rows_total{};
    std::size_t limit{};
};

struct FrameRecord {
    std::uint32_t frame{};
    std::uint32_t visit{};
    FrameGeneral general;
    std::optional<render::Composability> not_composable;
    std::vector<OccurrenceRow> rows;
    Measurements measurements;
    std::optional<RowOverflow> overflow;
};

[[nodiscard]] FrameRecord make_frame_record(std::uint32_t frame, std::uint32_t visit,
                                            FrameGeneral general,
                                            const render::RenderObservation &observation,
                                            Measurements measurements);

// RF-7.4, RF-7.5: processing time runs from the start of the frame input to its composed
// image; the instantaneous FPS comes from two consecutive presentations in continuous
// playback only. Values are never averaged.
enum class FrameContext { continuous, after_pause, navigation };

struct FrameTiming {
    std::uint32_t frame{};
    double input_started_ms{};
    double composed_ms{};
    double presented_ms{};
};

class FrameMeasurer final {
  public:
    [[nodiscard]] Measurements measure(const FrameTiming &timing, FrameContext context);

  private:
    std::optional<FrameTiming> previous_;
};

} // namespace ayther::replay_inspection
