#include "music_product_flow.h"

#include <cstdio>

int main() {
    using namespace ayther::runtime;
    MusicProductFlow flow;
    if (!flow.select_range({100, 500}) || !flow.author("The Battle") || !flow.preview() ||
        !flow.bake("pack-revision-18") || !flow.start_runtime() || !flow.pause_host(true) ||
        !flow.save("state-1") || !flow.pause_host(false))
        return 1;
    const auto replay = flow.accepted_state();
    if (replay.stage != ProductFlowStage::runtime || replay.cursor != 0 ||
        replay.pack_revision != "pack-revision-18" || replay.save != "state-1" ||
        flow.replay_id().empty())
        return 2;

    if (flow.select_range({500, 100}) || flow.cancel_analysis("revision changed") ||
        flow.accepted_state() != replay)
        return 3;

    if (!flow.navigate("take-2") || flow.accepted_state().take != "take-2" ||
        flow.navigation_id().empty() || flow.navigation_id() == flow.replay_id())
        return 4;
    std::puts("music_product_flow: replay and navigation passed");
    return 0;
}
