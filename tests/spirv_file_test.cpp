#include "vulkan_backend/spirv_file.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int passed = 0;
int failed = 0;

void check(const bool condition, const char* description) {
    if (condition) {
        ++passed;
    } else {
        ++failed;
    }
    std::fprintf(stderr, "  [%s] %s\n", condition ? " OK " : "FAIL", description);
}

void write_bytes(const std::filesystem::path& path, const void* bytes,
                 const std::size_t size) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (size != 0U) {
        output.write(static_cast<const char*>(bytes),
                     static_cast<std::streamsize>(size));
    }
}

}  // namespace

int main() {
    using ayther::runtime::vulkan::load_spirv_binary;
    using ayther::runtime::vulkan::SpirvReadError;

    std::fprintf(stderr, "== spirv_file_test (MAD-008) ==\n");

    const auto unique_suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path test_directory =
        std::filesystem::temp_directory_path() /
        ("ayther-runtime-mad008-spirv-" + std::to_string(unique_suffix));
    std::error_code filesystem_error;
    std::filesystem::create_directories(test_directory, filesystem_error);
    check(!filesystem_error, "temporary test directory is available");

    const std::array<std::uint32_t, 3> expected_words{0x07230203U, 0x00010000U,
                                                      0x12345678U};
    const auto valid_path = test_directory / "valid.spv";
    write_bytes(valid_path, expected_words.data(), sizeof(expected_words));
    const auto valid = load_spirv_binary(valid_path.string());
    check(static_cast<bool>(valid), "a complete aligned SPIR-V file loads");
    check(valid.words.size() == expected_words.size() &&
              valid.words[0] == expected_words[0] &&
              valid.words[1] == expected_words[1] &&
              valid.words[2] == expected_words[2],
          "every SPIR-V word is preserved");
    check(valid.expected_bytes == sizeof(expected_words) &&
              valid.actual_bytes == sizeof(expected_words),
          "the result records the complete byte count");
    check(std::filesystem::remove(valid_path, filesystem_error) && !filesystem_error,
          "the shader file handle is closed before return");

    const std::array<unsigned char, 3> malformed_bytes{1U, 2U, 3U};
    const auto malformed_path = test_directory / "malformed.spv";
    write_bytes(malformed_path, malformed_bytes.data(), malformed_bytes.size());
    const auto malformed = load_spirv_binary(malformed_path.string());
    check(!malformed && malformed.error == SpirvReadError::invalid_size,
          "a non-word-aligned SPIR-V file is rejected");

    const auto empty_path = test_directory / "empty.spv";
    write_bytes(empty_path, nullptr, 0U);
    const auto empty = load_spirv_binary(empty_path.string());
    check(!empty && empty.error == SpirvReadError::invalid_size,
          "an empty SPIR-V file is rejected");

    const auto missing = load_spirv_binary((test_directory / "missing.spv").string());
    check(!missing && missing.error == SpirvReadError::open_failed,
          "a missing SPIR-V file reports open failure");

    std::filesystem::remove_all(test_directory, filesystem_error);
    std::fprintf(stderr, "\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
