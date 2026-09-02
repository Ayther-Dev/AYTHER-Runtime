#include "runtime_config.h"

#include <cstdio>
#include <filesystem>

namespace {

int passed = 0;
int failed = 0;

void check(bool condition, const char* description) {
    if (condition) {
        ++passed;
    } else {
        ++failed;
    }
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", description);
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    using ayther::runtime::RuntimeConfig;
    using ayther::runtime::RuntimePaths;

    std::printf("== runtime_config_test (MIG-020) ==\n");

    const fs::path root = fs::path("test-data") / "Ayther" / "runtime";
    const RuntimePaths paths(root);

    check(paths.user_data_directory() == root,
          "the explicit Runtime data root is preserved");
    check(paths.configuration_directory() == root,
          "player configuration is owned by Runtime");
    check(paths.default_saves_directory() == root / "saves",
          "default saves live below Runtime data");
    check(paths.captures_directory() == root / "capturas",
          "captures live below Runtime data");
    check(paths.diagnostics_file() == root / "diagnostico.md",
          "diagnostics live below Runtime data");

    const RuntimeConfig defaults(paths);
    check(defaults.saves_directory() == root / "saves",
          "RuntimeConfig selects the Runtime save default");

    const fs::path launcher_root = fs::path("launcher-data") / "saves";
    const RuntimeConfig overridden(paths, launcher_root);
    check(overridden.saves_directory() == launcher_root,
          "the launcher save directory has precedence");
    check(overridden.paths().captures_directory() == root / "capturas",
          "a save override does not move captures");

    const RuntimePaths non_empty(fs::path{});
    check(!non_empty.user_data_directory().empty(),
          "an empty explicit root falls back to the current directory");
    const RuntimePaths discovered = RuntimePaths::discover();
    check(discovered.user_data_directory().is_absolute(),
          "platform discovery returns an absolute path");
    check(discovered.user_data_directory().filename() == "runtime"
              && discovered.user_data_directory().parent_path().filename() == "Ayther",
          "platform discovery scopes data below Ayther/runtime");

    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
