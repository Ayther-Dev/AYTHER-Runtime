#pragma once

#include <ayther/engine/render_observer.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Spec 002, contracts.md C3 (BR-134): the render observation 1.0 of the Engine package of G3.
// The enumerations and identifiers are those of the package. The views borrow producer
// storage only during the observer callback, so the Runtime copies them into this owned
// observation, which the pure modules keep.
namespace ayther::replay_inspection::render {

namespace engine_render = ayther::engine::render_observation;

using Availability = engine_render::Availability;
using Composability = engine_render::Composability;
using OccurrenceStatus = engine_render::OccurrenceStatus;
using DrawOutcome = engine_render::DrawOutcome;
using TextureState = engine_render::TextureState;
using OccurrenceId = engine_render::OccurrenceId;

inline constexpr std::size_t max_occurrences = engine_render::max_occurrences;

// A text field of the observation; `value` only when `known`.
struct TextField {
    Availability availability{Availability::unknown};
    std::string value;
    bool operator==(const TextField &) const = default;
};

struct ObservedOccurrence {
    OccurrenceId id;
    std::uint64_t identity_hash{};
    OccurrenceStatus status{OccurrenceStatus::original_unassigned};
    std::int32_t replacement{-1};
    TextField pose;
    TextField not_applied_reason;
};

struct ObservedReplacement {
    std::uint32_t index{};
    std::string kind;
    std::string pose_key;
    std::string asset;
    std::vector<std::uint16_t> members;
    Availability render_availability{Availability::unknown};
    DrawOutcome draw{DrawOutcome::discarded};
    TextureState texture{TextureState::pending};
};

struct RenderObservation {
    // C3: the frame position is copied as published; it is never invented.
    bool frame_known{};
    std::uint64_t emulation_frame{};
    Composability composability{Composability::composable};
    std::vector<ObservedOccurrence> occurrences;
    std::vector<ObservedReplacement> replacements;
    std::size_t occurrences_total{};
    std::size_t replacements_total{};
};

// Copies the borrowed view during the callback; the result owns all its text.
[[nodiscard]] RenderObservation copy_observation(const engine_render::RenderFrameView &view);

} // namespace ayther::replay_inspection::render
