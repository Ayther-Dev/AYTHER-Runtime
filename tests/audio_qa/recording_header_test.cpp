#include "recording_header.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace qa = ayther::audio_qa;
namespace {

using HeaderBytes = std::array<std::byte, qa::recording_fixed_header_bytes>;

void require(const bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

HeaderBytes header_for(const std::uint32_t version) {
    HeaderBytes bytes{std::byte{0x41}, std::byte{0x52}, std::byte{0x50}, std::byte{0x31}};
    for (std::size_t index = 0; index < sizeof(version); ++index) {
        bytes[4 + index] = static_cast<std::byte>((version >> (index * 8U)) & 0xffU);
    }
    return bytes;
}

void require_valid(const std::span<const std::byte> bytes, const std::uint32_t version,
                   const char *message) {
    const auto result = qa::decode_recording_header(bytes);
    require(result.error == qa::RecordingHeaderError::none && result.header.version == version,
            message);
}

void require_error(const std::span<const std::byte> bytes, const qa::RecordingHeaderError expected,
                   const std::string_view code, const char *message) {
    const auto result = qa::decode_recording_header(bytes);
    require(result.error == expected && qa::recording_header_error_code(result.error) == code,
            message);
}

} // namespace

int main() {
    try {
        for (std::uint32_t version = qa::oldest_supported_recording_version;
             version <= qa::current_recording_version; ++version) {
            const auto content = header_for(version);
            require_valid(content, version, "supported_version_rejected");
        }

        const auto same_content = header_for(qa::current_recording_version);
        require_valid(same_content, qa::current_recording_version, "ayr_content_rejected");
        require_valid(same_content, qa::current_recording_version, "arp_content_rejected");

        require_error({}, qa::RecordingHeaderError::empty_take, "empty_take",
                      "empty_take_not_diagnosed");
        for (std::size_t size = 1; size < qa::recording_fixed_header_bytes; ++size) {
            require_error(std::span<const std::byte>{same_content}.first(size),
                          qa::RecordingHeaderError::truncated_header, "truncated_header",
                          "truncated_header_not_diagnosed");
        }

        auto changed = same_content;
        changed[0] = std::byte{0};
        require_error(changed, qa::RecordingHeaderError::invalid_magic, "invalid_magic",
                      "extension_accepted_invalid_content");

        for (const auto version :
             {qa::oldest_supported_recording_version - 1U, qa::current_recording_version + 1U,
              std::uint32_t{0xffffffffU}}) {
            changed = header_for(version);
            const auto result = qa::decode_recording_header(changed);
            require(result.error == qa::RecordingHeaderError::unsupported_version &&
                        result.header.version == version &&
                        qa::recording_header_error_code(result.error) == "unsupported_version",
                    "unknown_version_not_diagnosed");
        }

        std::puts("recording_header_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "recording_header_test: %s\n", error.what());
        return 1;
    }
}
