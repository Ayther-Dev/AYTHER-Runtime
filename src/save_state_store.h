#pragma once

#include "runtime_error.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ayther::runtime {

enum class SaveStateStatus {
    saved,
    loaded,
    missing,
    invalid,
    unsupported_version,
    io_error,
    target_exists,
};

inline constexpr std::uint32_t save_state_format_version = 1;

enum class SaveStateWriteFault {
    none,
    disk_full,
    before_publish,
};

struct SaveStateLoadResult {
    SaveStateStatus status{SaveStateStatus::missing};
    std::uint32_t format_version{};
    std::vector<std::uint8_t> bytes;
    RuntimeErrorCode error{RuntimeErrorCode::state_restore_failed};
    std::string diagnostic;

    [[nodiscard]] bool loaded() const noexcept {
        return status == SaveStateStatus::loaded;
    }
};

struct SaveStateWriteResult {
    SaveStateStatus status{SaveStateStatus::io_error};
    std::filesystem::path path;
    RuntimeErrorCode error{RuntimeErrorCode::state_save_failed};
    std::string diagnostic;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == SaveStateStatus::saved;
    }
};

class SaveStateStore final {
public:
    explicit SaveStateStore(
        SaveStateWriteFault fault = SaveStateWriteFault::none) noexcept
        : fault_(fault) {}

    [[nodiscard]] SaveStateLoadResult load(
        const std::filesystem::path& path) const;

    [[nodiscard]] SaveStateWriteResult save(
        const std::filesystem::path& root, std::string_view game_id,
        std::string_view profile, std::string_view rom_revision,
        std::span<const std::uint8_t> bytes,
        std::string_view timestamp_utc = {}) const;

    [[nodiscard]] static std::string sanitize_component(
        std::string_view value);

private:
    SaveStateWriteFault fault_;
};

}  // namespace ayther::runtime
