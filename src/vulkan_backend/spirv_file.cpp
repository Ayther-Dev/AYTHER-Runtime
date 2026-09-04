#include "vulkan_backend/spirv_file.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ayther::runtime::vulkan {

namespace {

struct FileCloser final {
    void operator()(std::FILE* file) const noexcept {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

using UniqueFile = std::unique_ptr<std::FILE, FileCloser>;

}  // namespace

SpirvBinary load_spirv_binary(const std::string_view path) {
    const std::string owned_path{path};
    std::FILE* opened_file = nullptr;
#ifdef _WIN32
    (void)fopen_s(&opened_file, owned_path.c_str(), "rb");
#else
    opened_file = std::fopen(owned_path.c_str(), "rb");
#endif
    const UniqueFile file{opened_file};
    if (!file) {
        return {{}, SpirvReadError::open_failed, 0U, 0U};
    }
    if (std::fseek(file.get(), 0, SEEK_END) != 0) {
        return {{}, SpirvReadError::seek_failed, 0U, 0U};
    }

    const long file_size = std::ftell(file.get());
    if (file_size < 0) {
        return {{}, SpirvReadError::seek_failed, 0U, 0U};
    }
    if (file_size == 0 ||
        (file_size % static_cast<long>(sizeof(std::uint32_t))) != 0) {
        const std::size_t known_size = static_cast<std::size_t>(file_size);
        return {{}, SpirvReadError::invalid_size, known_size, 0U};
    }
    if (std::fseek(file.get(), 0, SEEK_SET) != 0) {
        return {
            {}, SpirvReadError::seek_failed, static_cast<std::size_t>(file_size), 0U};
    }

    const std::size_t expected_bytes = static_cast<std::size_t>(file_size);
    std::vector<std::uint32_t> words(expected_bytes / sizeof(std::uint32_t));
    const std::size_t actual_bytes =
        std::fread(words.data(), 1U, expected_bytes, file.get());
    if (actual_bytes != expected_bytes) {
        return {{}, SpirvReadError::short_read, expected_bytes, actual_bytes};
    }

    return {std::move(words), SpirvReadError::none, expected_bytes, actual_bytes};
}

}  // namespace ayther::runtime::vulkan
