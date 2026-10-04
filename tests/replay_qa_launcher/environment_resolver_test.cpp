// Spec 002, BR-165 (RF-1.6): the launcher resolves the Runtime installed beside it and the
// core of the Play CE configuration, read only, both with origin `environment`; when they
// are missing the field is flagged. Synthetic installations only.
#include "environment_resolver.h"
#include "launcher_model.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <system_error>

namespace la = ayther::replay_qa_launcher;
namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "ayther-qa-launcher-environment";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    const auto install = root / "install" / "bin";
    std::filesystem::create_directories(install);
    {
        std::ofstream{install / la::runtime_executable_name(), std::ios::binary} << "runtime";
    }
    const auto config = root / "appdata" / "Ayther" / "play_config.toml";
    std::filesystem::create_directories(config.parent_path());
    {
        std::ofstream{config, std::ios::binary}
            << "default_core = \"C:/cores/default.dll\"\n[cores]\n\"Mega Drive\" = "
               "\"C:/cores/gpgx.dll\"\n";
    }

    const auto found = la::resolve_environment(install, config, "C:/roms/game.md");
    expect(found.runtime && *found.runtime == (install / la::runtime_executable_name()).string(),
           "RF-1.6: the Runtime installed beside the launcher is found");
    expect(found.core == "C:/cores/gpgx.dll", "RF-1.6: the Play CE core of the platform is used");
    expect(la::resolve_environment(install, config, "C:/roms/game.sfc").core ==
               "C:/cores/default.dll",
           "RF-1.6: another platform falls back to default_core");

    la::LauncherModel model;
    model.set("--rom", "C:/roms/game.md");
    model.set("--output", "C:/evidence");
    model.add_take("C:/takes/a.ayr");
    model.set_environment(found);
    expect(model.request().runtime == *found.runtime &&
               model.request().core == "C:/cores/gpgx.dll" &&
               model.source_of("runtime") == qa::ValueSource::environment &&
               model.source_of("core") == qa::ValueSource::environment &&
               model.missing_required().empty(),
           "RF-1.6: environment values fill the fields with origin environment");

    const auto empty =
        la::resolve_environment(root / "nowhere", root / "missing.toml", "C:/roms/game.md");
    la::LauncherModel lacking;
    lacking.set("--rom", "C:/roms/game.md");
    lacking.set("--output", "C:/evidence");
    lacking.add_take("C:/takes/a.ayr");
    lacking.set_environment(empty);
    const auto missing = lacking.missing_required();
    expect(!empty.runtime && !empty.core && !missing.empty() &&
               missing.front().field == "--runtime" &&
               lacking.environment_issue("--core") == "core_unresolved",
           "RF-1.6: a missing Runtime or core is flagged in its field");
    std::filesystem::remove_all(root, ignored);
    if (failures != 0)
        return 1;
    std::cout << "the environment is resolved with its origin\n";
    return 0;
}
