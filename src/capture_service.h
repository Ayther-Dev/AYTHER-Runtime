#pragma once

#include "capture.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace ayther::runtime {

/// Validates and publishes synchronized capture sets as one operation.
class CaptureService final {
public:
    explicit CaptureService(
        const ayther::CaptureWriteOperations& operations =
            ayther::default_capture_write_operations()) noexcept
        : operations_(&operations) {}

    [[nodiscard]] ayther::CaptureWriteResult write(
        const std::filesystem::path& directory, std::string_view base,
        std::uint32_t width,
        std::uint32_t height, std::span<const std::uint8_t> original,
        std::span<const std::uint8_t> hd,
        float split, bool vertical,
        const ayther::CaptureMeta& metadata) const;

private:
    const ayther::CaptureWriteOperations* operations_;
};

}  // namespace ayther::runtime
