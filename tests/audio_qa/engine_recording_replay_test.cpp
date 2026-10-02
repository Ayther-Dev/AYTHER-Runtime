#include "engine_recording_replay.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;

int main() {
    try {
        const auto unavailable = qa::engine_recording_replay_operations(nullptr);
        if (unavailable.context != nullptr || unavailable.set_input != nullptr ||
            unavailable.step != nullptr) {
            throw std::runtime_error("missing_engine_session_exposed_operations");
        }
        std::puts("engine_recording_replay_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "engine_recording_replay_test: %s\n", error.what());
        return 1;
    }
}
