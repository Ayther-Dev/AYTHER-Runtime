// Spec 002, BR-076 (RF-2.13, RNF-5; contracts.md C2 «Evidencia completa»): the evidence of
// a traversal is complete only when identification, conditions, positions, controls,
// result and data were kept and checked without known losses. Omitting each part gives
// `incomplete(reason)`; a complete cancelled or inspected traversal is still not a take
// played in full.
#include "evidence_completeness.h"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

qa::EvidenceParts complete() {
    qa::EvidenceParts parts;
    parts.identification = true;
    parts.conditions = true;
    parts.positions = true;
    parts.controls = true;
    parts.result = true;
    parts.data_without_losses = true;
    parts.fragments_flushed = true;
    return parts;
}

} // namespace

int main() {
    const auto all = qa::evaluate_evidence(complete());
    expect(all.complete && all.reasons.empty(), "RF-2.13: every part kept is complete evidence");

    const std::vector<std::pair<bool qa::EvidenceParts::*, std::string>> parts{
        {&qa::EvidenceParts::identification, "identification_missing"},
        {&qa::EvidenceParts::conditions, "conditions_missing"},
        {&qa::EvidenceParts::positions, "positions_missing"},
        {&qa::EvidenceParts::controls, "controls_missing"},
        {&qa::EvidenceParts::result, "result_missing"},
        {&qa::EvidenceParts::data_without_losses, "data_lost"},
        {&qa::EvidenceParts::fragments_flushed, "fragments_not_flushed"},
    };
    for (const auto &[member, reason] : parts) {
        auto omitted = complete();
        omitted.*member = false;
        const auto evidence = qa::evaluate_evidence(omitted);
        expect(!evidence.complete && evidence.reasons == std::vector<std::string>{reason},
               "RF-2.13: omitting a part gives incomplete(" + reason + ")");
    }
    auto two = complete();
    two.positions = false;
    two.data_without_losses = false;
    expect(qa::evaluate_evidence(two).reasons ==
               std::vector<std::string>{"positions_missing", "data_lost"},
           "RF-2.13: every missing part is named, in a fixed order");

    // Complete evidence of a cancelled or inspected traversal is not a full linear take.
    expect(!qa::credits_full_take(all, qa::PlaybackKind::cancelled, qa::TraversalKind::linear) &&
               !qa::credits_full_take(all, qa::PlaybackKind::natural_end,
                                      qa::TraversalKind::inspection) &&
               qa::credits_full_take(all, qa::PlaybackKind::natural_end, qa::TraversalKind::linear),
           "RF-2.13: complete evidence of a cancellation or an inspection is not a full take");
    if (failures != 0)
        return 1;
    std::cout << "complete evidence needs every part\n";
    return 0;
}
