// Spec 002, DI-25 (RF-4.6, RNF-2; D-6b): an isolated late frame of continuous playback is
// recorded as a degraded cadence but does not make the audiovisual observation incomplete when
// it is at most 3 periods past its deadline and there is at most one per 1000 frames of the
// take (at least one). Any other effect on the presentation still does.
#include "replay_presentation.h"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

qa::ReplayPresentation visible() {
    qa::ReplayPresentation presentation;
    presentation.mode = "visible";
    presentation.code = "presented";
    return presentation;
}

} // namespace

int main() {
    constexpr std::uint32_t take = 7892U;

    {
        auto presentation = visible();
        for (const std::uint32_t frame : {382U, 424U, 1002U, 4273U})
            presentation.late(frame, 1.6, take);
        expect(presentation.code == "cadence_degraded" && presentation.affected_frames == 4U,
               "DI-25: tolerated late frames are still recorded as a degraded cadence");
        expect(presentation.complete(),
               "DI-25: four isolated late frames in 7892 keep the observation complete");
    }
    {
        auto presentation = visible();
        for (std::uint32_t frame = 100U; frame < 108U; ++frame)
            presentation.late(frame, 1.2, take);
        expect(!presentation.complete(),
               "DI-25: more than one late frame per 1000 makes the observation incomplete");
    }
    {
        auto presentation = visible();
        presentation.late(2000U, 3.5, take);
        expect(!presentation.complete(),
               "DI-25: a frame more than 3 periods late makes the observation incomplete");
    }
    {
        auto presentation = visible();
        presentation.late(2000U, 1.5, take);
        presentation.interrupted("window_minimized");
        expect(!presentation.complete(),
               "D-14: an interruption after a tolerated late frame is still incomplete");
    }
    {
        auto presentation = visible();
        presentation.late(2000U, 1.5, take);
        presentation.affect(2100U);
        expect(!presentation.complete(),
               "DI-25: an affected frame that is not a tolerated delay is incomplete");
    }
    {
        auto presentation = visible();
        presentation.late(4U, 2.0, 6U);
        expect(presentation.complete(), "DI-25: a short take tolerates one isolated late frame");
        presentation.late(5U, 2.0, 6U);
        expect(!presentation.complete(), "DI-25: a short take tolerates no more than one");
    }

    if (failures != 0)
        return 1;
    std::cout << "presentation tolerance: isolated late frames tolerated, others incomplete\n";
    return 0;
}
