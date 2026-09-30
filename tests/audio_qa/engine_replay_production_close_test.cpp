#include "engine_replay_production_close.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;

int main() {
    try {
        const auto unavailable = qa::engine_replay_production_close_operations(nullptr);
        if (unavailable.context != nullptr || unavailable.freeze != nullptr ||
            unavailable.finalize_voices != nullptr || unavailable.drain != nullptr) {
            throw std::runtime_error("missing_engine_session_exposed_close_operations");
        }
        std::puts("engine_replay_production_close_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "engine_replay_production_close_test: %s\n", error.what());
        return 1;
    }
}
