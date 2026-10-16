#pragma once

#include "effective_values.h"
#include "field_issue.h"
#include "material_preflight.h"
#include "runtime_probe.h"
#include "runtime_process.h"
#include "runtime_protocol_v11.h"

#include <optional>
#include <string>
#include <vector>

namespace ayther::audio_qa {

// Spec 002, plan §5.2 (RF-1.2, RF-1.7, RF-2.2, RNF-3; contracts.md C5): everything that
// is validated before the request is admitted. Each material is pinned, the takes are
// read from the pinned bytes, and the Runtime probes the core with the ROM and the pack.
// Nothing is written in the destination; the probes run with a private, temporary data
// root that is removed afterwards.
struct PreflightPins {
    MaterialPin runtime;
    MaterialPin rom;
    MaterialPin core;
    std::vector<MaterialPin> takes;
    std::vector<TakeFacts> take_facts;
    std::optional<MaterialPin> pack;
    std::optional<MaterialPin> trust_registry;
    RuntimeBinaryIdentity runtime_identity;
    CoreProbe core_probe;
    std::optional<PackProbe> pack_probe;
    // Contracts.md C1: the protocol negotiated for the presentation of the request.
    ContractVersion runtime_protocol;
    // Spec 006: the pad timelines, by position after the takes, with the bookmark state and
    // the `.fm` sidecar each one names. A state or sidecar pin keeps the position of its
    // timeline.
    std::vector<MaterialPin> timelines;
    std::vector<TimelineFacts> timeline_facts;
    std::vector<MaterialPin> timeline_states;
    std::vector<MaterialPin> timeline_fm_sidecars;
};

struct RequestPreflight {
    std::vector<FieldIssue> issues;
    // Only when there is no issue.
    std::optional<PreflightPins> pins;
    // Spec 006 (RF-2.3): `audio_qa_diagnostic:` lines that explain an issue, such as the
    // line of a timeline that does not parse.
    std::vector<std::string> diagnostics;
};

// Every pinned material, in a fixed order: runtime, ROM, core, takes by position,
// timelines by position (each with its state and sidecar), pack and trust registry.
[[nodiscard]] std::vector<MaterialPin> material_pins(const PreflightPins &pins);

// The materials one slot uses: runtime, ROM, core, the take or timeline at `position`
// (with its state and sidecar), pack and trust registry. They are held while that slot
// runs (RF-2.11).
[[nodiscard]] std::vector<MaterialPin> slot_material_pins(const PreflightPins &pins,
                                                          std::size_t position);
[[nodiscard]] inline std::vector<MaterialPin> take_material_pins(const PreflightPins &pins,
                                                                 std::size_t position) {
    return slot_material_pins(pins, position);
}

// The field of the take at `position`, as the issues name it: `--take[<position>]`.
[[nodiscard]] std::string take_field(std::size_t position);
// The field of the timeline at `index` in `--timeline` order: `--timeline[<index>]`.
[[nodiscard]] std::string timeline_field(std::size_t index);

[[nodiscard]] RequestPreflight preflight_request(const EffectiveRequest &request);

} // namespace ayther::audio_qa
