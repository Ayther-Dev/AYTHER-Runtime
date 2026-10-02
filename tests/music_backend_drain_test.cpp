#include "music_backend_drain.h"

int main() {
    runtime::MusicBackendDrain drain;
    if (!drain.request(100, 7) || drain.publish(8) || drain.request_frame() != 100 ||
        drain.old_generation() != 7)
        return 1;
    drain.record_emitted(480);
    drain.record_discarded(96);
    if (!drain.confirm(104, 108) || drain.confirmation_frame() != 104 ||
        drain.boundary_frame() != 108 || !drain.publish(8) || drain.new_generation() != 8)
        return 2;
    if (drain.emitted_irreversible_frames() != 480 || drain.discarded_frames() != 96 ||
        !drain.discard_traced())
        return 3;

    runtime::MusicBackendDrain failed;
    if (!failed.request(200, 11) || failed.confirm(199, 201) || failed.confirm(202, 201) ||
        failed.publish(12))
        return 4;
    return 0;
}
