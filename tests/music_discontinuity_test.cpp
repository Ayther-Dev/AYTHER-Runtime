#include "music_discontinuity.h"

int main() {
    runtime::MusicDiscontinuityState state;
    if (!state.begin_take(1) || !state.observe_pattern(100, 120) || state.entry_count() != 1)
        return 1;
    if (!state.navigate(500) || !state.discontinuity_recorded() || state.link_across_jump(120, 500))
        return 2;
    if (!state.begin_take(2) || state.entry_count() != 0 || state.discontinuity_recorded() ||
        state.take_id() != 2)
        return 3;

    state.seed_active_music(3, 4, 5);
    if (!state.load_legacy_save() || state.active_music() != 0 || state.candidate_count() != 0 ||
        state.pending_count() != 0 || !state.original_audio())
        return 4;
    state.close_partial();
    if (state.exact_entry_count_claimed() || state.synthetic_close_frames() != 0)
        return 5;
    return 0;
}
