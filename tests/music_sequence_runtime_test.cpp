#include "runtime_music_sequence.h"

#include <cstdio>

int main() {
    ayther::engine::MusicSequenceDefinition definition;
    definition.identity = ayther::engine::MusicIdentityId{1};
    definition.bus = ayther::engine::AudioBusId{1};
    definition.entry_node = ayther::engine::SequenceNodeId{10};
    definition.segments = {{ayther::engine::SequenceSegmentId{20}, "Intro"}};
    definition.assignments = {{ayther::engine::AssetAssignmentId{30},
                               ayther::engine::SequenceSegmentId{20},
                               ayther::engine::AssetId{40},
                               {0, 100}}};
    definition.nodes = {{ayther::engine::SequenceNodeId{10}, ayther::engine::SequenceSegmentId{20},
                         ayther::engine::AssetAssignmentId{30}}};

    runtime::MusicSequenceRuntime runtime{definition};
    if (!runtime.start(ayther::engine::OccurrenceId{50}) ||
        runtime.position().identity != definition.identity) {
        std::fprintf(stderr, "FAIL: Runtime does not consume the public model\n");
        return 1;
    }
    return 0;
}
