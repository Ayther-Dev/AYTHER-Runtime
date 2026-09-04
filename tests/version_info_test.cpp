#include "version_info.h"

#include <ayther/engine/capabilities.hpp>

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(const bool condition, const char* description) {
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", description);
    if (!condition) {
        ++failures;
    }
}

}  // namespace

int main() {
    const auto linked = ayther::engine::version();
    const std::string expected_engine =
        std::to_string(linked.major) + "." +
        std::to_string(linked.minor) + "." +
        std::to_string(linked.patch);

    check(ayther::runtime::runtime_version == "0.1.0-beta.1",
          "Runtime reports its product version and release channel");
    check(ayther::runtime::linked_engine_version() == expected_engine,
          "Engine version comes from the linked Engine API");
    check(ayther::runtime::version_report() ==
              "AYTHER Runtime 0.1.0-beta.1; Engine " + expected_engine,
          "the startup log reports both component versions");

    const std::string title =
        ayther::runtime::window_title("Vulkan Passthrough");
    check(title == "AYTHER Runtime 0.1.0-beta.1 — Engine " + expected_engine +
                       " — Vulkan Passthrough",
          "the window title reports Runtime and Engine versions");

    std::printf("\n%d passed, %d failed\n", 4 - failures, failures);
    return failures == 0 ? 0 : 1;
}
