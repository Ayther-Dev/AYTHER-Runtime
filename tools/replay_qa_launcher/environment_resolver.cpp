#include "environment_resolver.h"

#include "effective_values.h"

#include <system_error>

namespace ayther::replay_qa_launcher {

std::string_view runtime_executable_name() noexcept {
#ifdef _WIN32
    return "ayther_runtime.exe";
#else
    return "ayther_runtime";
#endif
}

EnvironmentValues resolve_environment(const std::filesystem::path &launcher_directory,
                                      const std::filesystem::path &play_config,
                                      std::string_view rom) {
    EnvironmentValues values;
    const auto runtime = launcher_directory / runtime_executable_name();
    std::error_code error;
    if (std::filesystem::is_regular_file(runtime, error) && !error)
        values.runtime = runtime.string();
    if (!rom.empty())
        values.core = audio_qa::read_play_config_core(play_config, rom);
    return values;
}

} // namespace ayther::replay_qa_launcher
