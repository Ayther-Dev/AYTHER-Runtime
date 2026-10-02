#include "music_restore_transaction.h"

int main() {
    runtime::MusicRestoreTransaction transaction{7, 100, true, false};
    if (!transaction.prepare(8, 120, false) || transaction.visible_generation() != 7 ||
        !transaction.host_paused() || transaction.game_music_paused())
        return 1;
    if (transaction.publish(119) || !transaction.publish(120) ||
        transaction.visible_generation() != 8 || !transaction.host_paused() ||
        !transaction.game_music_paused() || !transaction.states_visible_together())
        return 2;
    if (transaction.accepts_decision(7) || transaction.accepts_pcm(7, 120) ||
        !transaction.accepts_decision(8) || !transaction.accepts_pcm(8, 120))
        return 3;

    runtime::MusicRestoreTransaction during_link{20, 300, false, true};
    during_link.set_link_active(true);
    if (!during_link.prepare(21, 310, false) || !during_link.publish(310) ||
        during_link.link_active() || during_link.accepts_pcm(20, 311))
        return 4;
    return 0;
}
