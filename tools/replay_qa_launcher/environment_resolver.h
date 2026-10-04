#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ayther::replay_qa_launcher {

// Spec 002, plan §5.1 and §5.14 (RF-1.6): what the environment provides without the user
// typing it: the Runtime installed beside the launcher and the core of the Play CE
// configuration (`cores[platform]`, then `default_core`), which is only read.
struct EnvironmentValues {
    std::optional<std::string> runtime;
    std::optional<std::string> core;
};

[[nodiscard]] std::string_view runtime_executable_name() noexcept;

[[nodiscard]] EnvironmentValues resolve_environment(const std::filesystem::path &launcher_directory,
                                                    const std::filesystem::path &play_config,
                                                    std::string_view rom);

} // namespace ayther::replay_qa_launcher
