#include "engine_hd_initialization.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;

int main() {
    try {
        const auto absent = qa::initialize_engine_hd_audio(nullptr, "game-state", nullptr);
        if (absent.initialization != qa::HdInitialization::unknown || absent.restore.attempted ||
            !absent.fresh.attempted || absent.fresh.code != "engine_session_missing" ||
            !absent.evidence_incomplete) {
            throw std::runtime_error("missing_session_fresh_failure_not_preserved");
        }

        qa::EngineHdStateBundle partial;
        const auto supplied = qa::initialize_engine_hd_audio(nullptr, "game-state", &partial);
        if (supplied.initialization != qa::HdInitialization::unknown ||
            !supplied.restore.attempted || supplied.restore.code != "engine_session_missing" ||
            !supplied.fresh.attempted || supplied.fresh.code != "engine_session_missing" ||
            !supplied.evidence_incomplete) {
            throw std::runtime_error("missing_session_restore_failure_not_preserved");
        }

        std::puts("engine_hd_initialization_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "engine_hd_initialization_test: %s\n", error.what());
        return 1;
    }
}
