#pragma once

#include "fact_model.h"
#include "runtime_protocol_v11.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

// Spec 002, contracts.md C2 (RF-2.13): the fact classes the inspection adds, read with
// their typed fields. A measure the Runtime could not take stays unknown (RF-7.7).
struct RenderFrameFact {
    std::uint64_t frame{};
    std::uint64_t visit{};
    bool composable{};
    // C3 reason when the frame cannot be composed.
    std::string not_composable_reason;
    std::uint64_t occurrences{};
    std::optional<double> processing_ms;
    std::optional<double> fps_instant;
    bool operator==(const RenderFrameFact &) const = default;
};

struct RenderSummaryFact {
    std::uint64_t frame{};
    std::uint64_t occurrences{};
    std::uint64_t replaced{};
    std::uint64_t original_unassigned{};
    std::uint64_t assigned_not_applied{};
    std::uint64_t texture_not_ready{};
    bool operator==(const RenderSummaryFact &) const = default;
};

[[nodiscard]] std::optional<RenderFrameFact> read_render_frame(const Fact &fact);
[[nodiscard]] std::optional<RenderSummaryFact> read_render_summary(const Fact &fact);

struct RunInspectionFacts {
    std::vector<InspectionEvent> inspection_events;
    std::vector<RenderFrameFact> render_frames;
    std::vector<RenderSummaryFact> render_summaries;
};

enum class InspectionFactsError { unreadable_fragment, invalid_fact };
using RunInspectionFactsResult = std::variant<RunInspectionFacts, InspectionFactsError>;

// Reads every `fragments/*.aqf` of a run, in sequence order, and keeps the new classes.
// A fact of one of these classes that does not have its fields makes the read fail.
[[nodiscard]] RunInspectionFactsResult
read_run_inspection_facts(const std::filesystem::path &run_directory);

} // namespace ayther::audio_qa
