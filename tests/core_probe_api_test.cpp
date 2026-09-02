#include <ayther/engine/core_probe.hpp>

#include <cstdio>
#include <string>
#include <type_traits>

int main() {
    static_assert(!std::is_copy_constructible_v<ayther::engine::CoreProbe>);
    static_assert(std::is_nothrow_move_constructible_v<ayther::engine::CoreProbe>);

    const ayther::engine::CoreInfo info{
        .api_version = 1U,
        .library_name = "Runtime \"probe\"\n",
        .library_version = "test",
        .valid_extensions = "md|bin",
    };
    const std::string serialized = info.serialize();
    if (serialized.find("\"api\":1") == std::string::npos ||
        serialized.find("Runtime \\\"probe\\\"\\n") == std::string::npos ||
        serialized.find("\"valid_extensions\":\"md|bin\"") ==
            std::string::npos) {
        std::fprintf(stderr, "CoreInfo did not produce the expected JSON\n");
        return 1;
    }

    const auto missing = ayther::engine::probe_core(AYTHER_MISSING_CORE_PATH);
    if (missing || missing.error.code != ayther::ErrorCode::Io ||
        missing.error.message.empty()) {
        std::fprintf(stderr, "CoreProbe did not preserve the loader failure\n");
        return 1;
    }

    return 0;
}
