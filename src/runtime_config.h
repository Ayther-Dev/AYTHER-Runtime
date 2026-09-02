#pragma once
// ---------------------------------------------------------------------------
// runtime_config.h — Runtime-owned persistence locations.
//
// Engine configuration describes authoring and emulation concerns. Runtime
// only needs stable locations for its own player settings, captures,
// diagnostics, and save states, so that policy lives behind these value types.
// Discovery is independent of SDL and can run before core probing.
// ---------------------------------------------------------------------------

#include <filesystem>

namespace ayther::runtime {

class RuntimePaths final {
public:
    /// Build paths below an explicit Runtime data directory.
    explicit RuntimePaths(std::filesystem::path user_data_directory);

    /// Discover the platform user-data directory without initializing SDL.
    [[nodiscard]] static RuntimePaths discover();

    [[nodiscard]] const std::filesystem::path& user_data_directory() const noexcept;
    [[nodiscard]] const std::filesystem::path& configuration_directory() const noexcept;
    [[nodiscard]] std::filesystem::path default_saves_directory() const;
    [[nodiscard]] std::filesystem::path captures_directory() const;
    [[nodiscard]] std::filesystem::path diagnostics_file() const;

private:
    std::filesystem::path user_data_directory_;
};

class RuntimeConfig final {
public:
    /// Resolve effective Runtime locations. A non-empty launch save directory
    /// has precedence over the platform default selected by RuntimePaths.
    explicit RuntimeConfig(
        RuntimePaths paths,
        std::filesystem::path launch_saves_directory = {});

    [[nodiscard]] const RuntimePaths& paths() const noexcept;
    [[nodiscard]] const std::filesystem::path& saves_directory() const noexcept;

private:
    RuntimePaths paths_;
    std::filesystem::path saves_directory_;
};

}  // namespace ayther::runtime
