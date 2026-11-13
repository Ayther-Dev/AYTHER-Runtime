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

// Spec 008 (RF-4.1, RF-6.1): version 2 records the emulator generation that
// wrote the state. Version 1 files and files without the magic were all
// written by gpgx and read as `gpgx / 1`.
inline constexpr std::uint32_t save_state_format_version = 2;
inline constexpr std::size_t save_state_max_emulator_id = 32;

struct SaveStateGeneration {
    std::string emulator_id{"gpgx"};
    std::uint32_t determinism_contract{1};

    friend bool operator==(const SaveStateGeneration&,
                           const SaveStateGeneration&) = default;
};

enum class SaveStateWriteFault {
    none,
    disk_full,
    before_publish,
};

struct SaveStateLoadResult {
    SaveStateStatus status{SaveStateStatus::missing};
    std::uint32_t format_version{};
    SaveStateGeneration generation;
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
        std::string_view timestamp_utc = {},
        const SaveStateGeneration& generation = {}) const;

    [[nodiscard]] static std::string sanitize_component(
        std::string_view value);

private:
    SaveStateWriteFault fault_;
};

}  // namespace ayther::runtime
