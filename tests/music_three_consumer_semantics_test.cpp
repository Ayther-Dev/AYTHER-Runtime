#include "app/music_authoring_adapter.h"
#include "app/music_preview_adapter.h"
#include "runtime_music_adapter.h"

#include <array>
#include <cstdio>

namespace {
ayther::engine::MusicSequenceDefinition definition() {
    using namespace ayther::engine;
    MusicSequenceDefinition value;
    value.identity = MusicIdentityId{101};
    value.bus = AudioBusId{102};
    value.entry_node = SequenceNodeId{103};
    value.segments = {{SequenceSegmentId{104}, "Intro"}, {SequenceSegmentId{105}, "Loop"}};
    value.assignments = {{AssetAssignmentId{106}, SequenceSegmentId{104}, AssetId{107}, {10, 110}},
                         {AssetAssignmentId{108}, SequenceSegmentId{105}, AssetId{109}, {20, 220}}};
    value.nodes = {{SequenceNodeId{103}, SequenceSegmentId{104}, AssetAssignmentId{106}},
                   {SequenceNodeId{110}, SequenceSegmentId{105}, AssetAssignmentId{108}}};
    value.edges = {{SequenceEdgeId{111}, SequenceNodeId{103}, SequenceNodeId{110}}};
    return value;
}
} // namespace

int main() {
    const auto sequence = definition();
    lab::MusicAuthoringAdapter authoring{sequence};
    lab::MusicPreviewAdapter preview{sequence};
    runtime::MusicRuntimeAdapter runtime{sequence};
    std::array adapters{&authoring.policy(), &preview.policy(), &runtime.policy()};
    for (auto *adapter : adapters) {
        if (!adapter->start({200}) || !adapter->transition({110}, 300))
            return 1;
        adapter->advance(48);
    }
    const auto expected = adapters.front()->semantic_state();
    for (auto *adapter : adapters) {
        if (adapter->semantic_state() != expected ||
            adapter->last_reason() != "transition_selected" || adapter->music_cursor() != 48 ||
            adapter->active_region() != ayther::engine::SampleRegion{20, 220} ||
            adapter->offline_analysis_calls() != 0)
            return 2;
    }
    std::puts("music_three_consumer_semantics: zero divergences");
    return 0;
}
