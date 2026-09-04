#include "capture_service.h"

namespace ayther::runtime {

ayther::CaptureWriteResult CaptureService::write(
    const std::filesystem::path& directory, const std::string_view base,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::span<const std::uint8_t> original,
    const std::span<const std::uint8_t> hd, const float split,
    const bool vertical, const ayther::CaptureMeta& metadata) const {
    const auto required = ayther::capture_pixel_bytes(width, height);
    if (!required || original.size() != *required || hd.size() != *required) {
        return {{}, ayther::CaptureWriteError::invalid_input};
    }
    return ayther::capture_write_transactional(
        directory, std::string(base), original.data(), hd.data(), width, height,
        split, vertical, metadata, *operations_);
}

}  // namespace ayther::runtime
