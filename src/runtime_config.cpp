#include "runtime_config.h"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

namespace ayther::runtime {
namespace {

#ifdef _WIN32
std::filesystem::path environment_path(const wchar_t* name) {
    wchar_t* raw = nullptr;
    std::size_t size = 0;
    if (_wdupenv_s(&raw, &size, name) != 0 || raw == nullptr || size <= 1) {
        std::free(raw);
        return {};
    }

    const std::unique_ptr<wchar_t, decltype(&std::free)> value(raw, &std::free);
    return std::filesystem::path(value.get());
}
#else
std::filesystem::path environment_path(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0'
        ? std::filesystem::path(value)
        : std::filesystem::path{};
}
#endif

std::filesystem::path current_directory_fallback() {
    std::error_code error;
    const auto current = std::filesystem::current_path(error);
    return error ? std::filesystem::path{"."} : current;
}

std::filesystem::path platform_data_home() {
#ifdef _WIN32
    auto base = environment_path(L"APPDATA");
    if (base.empty()) {
        const auto profile = environment_path(L"USERPROFILE");
        if (!profile.empty()) {
            base = profile / "AppData" / "Roaming";
        }
    }
#elif defined(__APPLE__)
    auto base = environment_path("HOME");
    if (!base.empty()) {
        base /= "Library/Application Support";
    }
#else
    auto base = environment_path("XDG_DATA_HOME");
    if (base.empty()) {
        const auto home = environment_path("HOME");
        if (!home.empty()) {
            base = home / ".local" / "share";
        }
    }
#endif

    return base.empty() ? current_directory_fallback() : base;
}

}  // namespace

RuntimePaths::RuntimePaths(std::filesystem::path user_data_directory)
    : user_data_directory_(user_data_directory.empty()
          ? std::filesystem::path{"."}
          : std::move(user_data_directory)) {}

RuntimePaths RuntimePaths::discover() {
    auto directory = platform_data_home() / "Ayther" / "runtime";
    if (directory.is_relative()) {
        std::error_code error;
        const auto absolute = std::filesystem::absolute(directory, error);
        if (!error) {
            directory = absolute;
        }
    }
    return RuntimePaths(std::move(directory));
}

const std::filesystem::path& RuntimePaths::user_data_directory() const noexcept {
    return user_data_directory_;
}

const std::filesystem::path& RuntimePaths::configuration_directory() const noexcept {
    return user_data_directory_;
}

std::filesystem::path RuntimePaths::default_saves_directory() const {
    return user_data_directory_ / "saves";
}

std::filesystem::path RuntimePaths::captures_directory() const {
    return user_data_directory_ / "capturas";
}

std::filesystem::path RuntimePaths::diagnostics_file() const {
    return user_data_directory_ / "diagnostico.md";
}

RuntimeConfig::RuntimeConfig(
    RuntimePaths paths,
    std::filesystem::path launch_saves_directory)
    : paths_(std::move(paths)),
      saves_directory_(launch_saves_directory.empty()
          ? paths_.default_saves_directory()
          : std::move(launch_saves_directory)) {}

const RuntimePaths& RuntimeConfig::paths() const noexcept {
    return paths_;
}

const std::filesystem::path& RuntimeConfig::saves_directory() const noexcept {
    return saves_directory_;
}

}  // namespace ayther::runtime
