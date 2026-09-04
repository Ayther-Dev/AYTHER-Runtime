// ---------------------------------------------------------------------------
// capture.cpp — see capture.h for the public contract.
// ---------------------------------------------------------------------------
#include "capture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Runtime owns its PNG writer. Keep stb's definitions local to this translation
// unit so they cannot collide with implementation details from Engine.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace ayther {
namespace {

[[nodiscard]] bool checked_pixel_bytes(const std::uint32_t width,
                                       const std::uint32_t height,
                                       std::size_t& output) noexcept {
    constexpr std::size_t channels = 4U;
    if (width == 0U || height == 0U ||
        width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    const std::size_t row = static_cast<std::size_t>(width) * channels;
    if (row > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        static_cast<std::size_t>(height) >
            std::numeric_limits<std::size_t>::max() / row) {
        return false;
    }
    output = row * static_cast<std::size_t>(height);
    return true;
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(value.size() + 8U);
    for (const unsigned char code_unit : value) {
        switch (code_unit) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (code_unit < 0x20U) {
                output += "\\u00";
                output.push_back(hex[(code_unit >> 4U) & 0x0fU]);
                output.push_back(hex[code_unit & 0x0fU]);
            } else {
                output.push_back(static_cast<char>(code_unit));
            }
            break;
        }
    }
    return output;
}

[[nodiscard]] bool bgra_to_rgba(std::vector<std::uint8_t>& output,
                                const std::uint8_t* source,
                                const std::size_t bytes) {
    if (source == nullptr || bytes == 0U || bytes % 4U != 0U) {
        return false;
    }
    output.resize(bytes);
    for (std::size_t index = 0U; index < bytes; index += 4U) {
        output[index] = source[index + 2U];
        output[index + 1U] = source[index + 1U];
        output[index + 2U] = source[index];
        output[index + 3U] = 255U;
    }
    return true;
}

[[nodiscard]] bool write_png(const std::filesystem::path& path,
                             const std::uint8_t* pixels,
                             const std::uint32_t width,
                             const std::uint32_t height) {
    return stbi_write_png(path.string().c_str(), static_cast<int>(width),
                          static_cast<int>(height), 4, pixels,
                          static_cast<int>(width * 4U)) != 0;
}

[[nodiscard]] bool write_text(const std::filesystem::path& path,
                              const std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    const bool succeeded = static_cast<bool>(output);
    output.close();
    return succeeded && !output.fail();
}

[[nodiscard]] bool rename_file(const std::filesystem::path& source,
                               const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
}

void remove_file(const std::filesystem::path& path) noexcept {
    std::error_code error;
    std::filesystem::remove(path, error);
}

}  // namespace

std::optional<std::size_t>
capture_pixel_bytes(const std::uint32_t width,
                    const std::uint32_t height) noexcept {
    std::size_t bytes = 0U;
    if (!checked_pixel_bytes(width, height, bytes)) {
        return std::nullopt;
    }
    return bytes;
}

bool compose_split(std::vector<std::uint8_t>& destination,
                   const std::uint8_t* original, const std::uint8_t* enhanced,
                   const std::uint32_t width, const std::uint32_t height,
                   float split, const bool vertical) {
    std::size_t bytes = 0U;
    if (original == nullptr || enhanced == nullptr ||
        !checked_pixel_bytes(width, height, bytes)) {
        return false;
    }
    if (split < 0.0F) split = 0.0F;
    if (split > 1.0F) split = 1.0F;
    try {
        destination.resize(bytes);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }

    if (!vertical) {
        const std::uint32_t cut = static_cast<std::uint32_t>(width * split);
        for (std::uint32_t y = 0U; y < height; ++y) {
            const std::size_t row = static_cast<std::size_t>(y) * width * 4U;
            if (cut != 0U) {
                std::memcpy(destination.data() + row, original + row,
                            static_cast<std::size_t>(cut) * 4U);
            }
            if (cut < width) {
                const std::size_t offset = row + static_cast<std::size_t>(cut) * 4U;
                std::memcpy(destination.data() + offset, enhanced + offset,
                            static_cast<std::size_t>(width - cut) * 4U);
            }
        }
    } else {
        const std::uint32_t cut = static_cast<std::uint32_t>(height * split);
        const std::size_t stride = static_cast<std::size_t>(width) * 4U;
        if (cut != 0U) {
            std::memcpy(destination.data(), original,
                        static_cast<std::size_t>(cut) * stride);
        }
        if (cut < height) {
            const std::size_t offset = static_cast<std::size_t>(cut) * stride;
            std::memcpy(destination.data() + offset, enhanced + offset,
                        static_cast<std::size_t>(height - cut) * stride);
        }
    }
    return true;
}

std::string capture_metadata_json(const CaptureMeta& metadata) {
    std::ostringstream output;
    output << "{\n";
    output << "  \"capture\": \"ayther.compare\",\n";
    output << "  \"capture_version\": 1,\n";
    output << "  \"game_id\": \"" << json_escape(metadata.game_id) << "\",\n";
    output << "  \"pack\": " << (metadata.pack_name.empty()
                  ? "null" : "\"" + json_escape(metadata.pack_name) + "\"") << ",\n";
    output << "  \"pack_build\": " << (metadata.pack_build.empty()
                  ? "null" : "\"" + json_escape(metadata.pack_build) + "\"") << ",\n";
    output << "  \"profile\": " << (metadata.profile.empty()
                  ? "null" : "\"" + json_escape(metadata.profile) + "\"") << ",\n";
    output << "  \"timestamp\": \"" << json_escape(metadata.timestamp) << "\",\n";
    output << "  \"width\": " << metadata.width << ", \"height\": "
           << metadata.height << ",\n";
    output << "  \"images\": [\"original\", \"ayther\", \"split\"]\n";
    output << "}\n";
    return output.str();
}

const CaptureWriteOperations& default_capture_write_operations() noexcept {
    static constexpr CaptureWriteOperations operations{
        &write_png, &write_text, &rename_file, &remove_file};
    return operations;
}

CaptureWriteResult capture_write_transactional(
    const std::filesystem::path& directory, const std::string& base,
    const std::uint8_t* original, const std::uint8_t* enhanced,
    const std::uint32_t width, const std::uint32_t height, const float split,
    const bool vertical, const CaptureMeta& metadata,
    const CaptureWriteOperations& operations) {
    std::size_t bytes = 0U;
    const std::filesystem::path base_path{base};
    if (original == nullptr || enhanced == nullptr || base.empty() ||
        base == "." || base == ".." || base_path.filename() != base_path ||
        operations.write_png == nullptr || operations.write_text == nullptr ||
        operations.rename_file == nullptr || operations.remove_file == nullptr) {
        return {{}, CaptureWriteError::invalid_input};
    }
    if (!checked_pixel_bytes(width, height, bytes)) {
        return {{}, CaptureWriteError::size_overflow};
    }

    std::error_code filesystem_error;
    std::filesystem::create_directories(directory, filesystem_error);
    if (filesystem_error) {
        return {{}, CaptureWriteError::directory_error};
    }

    const std::array<std::filesystem::path, 4U> final_paths{
        directory / (base + "-original.png"),
        directory / (base + "-ayther.png"),
        directory / (base + "-split.png"),
        directory / (base + ".json")};
    std::array<std::filesystem::path, final_paths.size()> temporary_paths{};
    for (std::size_t index = 0U; index < final_paths.size(); ++index) {
        if (std::filesystem::exists(final_paths[index], filesystem_error) ||
            filesystem_error) {
            return {{}, filesystem_error ? CaptureWriteError::directory_error
                                         : CaptureWriteError::target_exists};
        }
        temporary_paths[index] = final_paths[index];
        temporary_paths[index] += ".tmp";
        operations.remove_file(temporary_paths[index]);
    }

    const auto cleanup = [&]() noexcept {
        for (const auto& path : temporary_paths) operations.remove_file(path);
        for (const auto& path : final_paths) operations.remove_file(path);
    };

    try {
        std::vector<std::uint8_t> original_rgba;
        std::vector<std::uint8_t> enhanced_rgba;
        std::vector<std::uint8_t> split_bgra;
        std::vector<std::uint8_t> split_rgba;
        if (!bgra_to_rgba(original_rgba, original, bytes) ||
            !bgra_to_rgba(enhanced_rgba, enhanced, bytes) ||
            !compose_split(split_bgra, original, enhanced, width, height,
                           split, vertical) ||
            !bgra_to_rgba(split_rgba, split_bgra.data(), bytes)) {
            cleanup();
            return {{}, CaptureWriteError::allocation_failed};
        }

        const std::array<const std::vector<std::uint8_t>*, 3U> images{
            &original_rgba, &enhanced_rgba, &split_rgba};
        for (std::size_t index = 0U; index < images.size(); ++index) {
            if (!operations.write_png(temporary_paths[index],
                                      images[index]->data(), width, height)) {
                cleanup();
                return {{}, CaptureWriteError::image_write_failed};
            }
        }
        const std::string metadata_json = capture_metadata_json(metadata);
        if (!operations.write_text(temporary_paths[3U], metadata_json)) {
            cleanup();
            return {{}, CaptureWriteError::metadata_write_failed};
        }
    } catch (const std::bad_alloc&) {
        cleanup();
        return {{}, CaptureWriteError::allocation_failed};
    } catch (const std::length_error&) {
        cleanup();
        return {{}, CaptureWriteError::allocation_failed};
    }

    for (std::size_t index = 0U; index < final_paths.size(); ++index) {
        if (!operations.rename_file(temporary_paths[index], final_paths[index])) {
            cleanup();
            return {{}, CaptureWriteError::rename_failed};
        }
    }
    return {(directory / base).string(), CaptureWriteError::none};
}

std::string capture_write(const std::filesystem::path& directory,
                          const std::string& base,
                          const std::uint8_t* original,
                          const std::uint8_t* enhanced,
                          const std::uint32_t width,
                          const std::uint32_t height, const float split,
                          const bool vertical, const CaptureMeta& metadata) {
    return capture_write_transactional(directory, base, original, enhanced,
                                       width, height, split, vertical, metadata)
        .prefix;
}

}  // namespace ayther
