#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ayther::runtime::vulkan {

enum class SpirvReadError {
    none,
    open_failed,
    seek_failed,
    invalid_size,
    short_read,
};

struct [[nodiscard]] SpirvBinary final {
    std::vector<std::uint32_t> words;
    SpirvReadError error{SpirvReadError::none};
    std::size_t expected_bytes{};
    std::size_t actual_bytes{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == SpirvReadError::none;
    }
};

[[nodiscard]] SpirvBinary load_spirv_binary(std::string_view path);

}  // namespace ayther::runtime::vulkan
