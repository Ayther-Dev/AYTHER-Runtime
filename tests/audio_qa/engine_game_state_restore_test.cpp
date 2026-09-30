#include "engine_game_state_restore.h"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace qa = ayther::audio_qa;

int main() {
    try {
        const std::vector<std::uint8_t> state{1, 2, 3, 4};
        const auto result = qa::restore_engine_game_state(nullptr, state);
        if (result.succeeded || result.code != "engine_session_missing" || result.detail.empty()) {
            throw std::runtime_error("missing_engine_session_not_preserved");
        }
        std::puts("engine_game_state_restore_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "engine_game_state_restore_test: %s\n", error.what());
        return 1;
    }
}
