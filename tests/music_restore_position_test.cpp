#include "music_restore_position.h"

int main() {
    using runtime::MusicRestorePositionInput;
    using runtime::MusicRestorePositionResolver;
    MusicRestorePositionResolver resolver;

    for (const auto node : {runtime::RestoredMusicNode::intro, runtime::RestoredMusicNode::loop,
                            runtime::RestoredMusicNode::link}) {
        MusicRestorePositionInput input;
        input.old_generation = 5;
        input.new_generation = 6;
        input.occurrence = 10;
        input.appearance = 11;
        input.visit = 3;
        input.iteration = 7;
        input.node = node;
        input.source_cursor = 24'001;
        input.envelope_cursor = 240;
        input.source_rate = 48'000;
        input.output_rate = 48'000;
        input.host_paused = true;
        const auto exact = resolver.restore(input);
        if (!exact || exact->output_cursor != 24'001 || exact->occurrence != 10 ||
            exact->appearance != 11 || exact->visit != 3 || exact->iteration != 7 ||
            exact->envelope_cursor != 240 || !exact->host_paused || exact->new_visit ||
            exact->new_trigger)
            return 1;

        input.output_rate = 44'100;
        const auto converted = resolver.restore(input);
        const std::uint64_t expected = 22'051;
        if (!converted || converted->output_cursor != expected ||
            converted->rational_numerator != 24'001ULL * 44'100ULL ||
            converted->rational_denominator != 48'000 || converted->conversion_error_samples > 1)
            return 2;
    }
    return 0;
}
