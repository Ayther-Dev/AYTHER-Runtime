#include "protocol_header.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace qa = ayther::audio_qa;
namespace {

template <typename UInt>
void write_le(std::array<std::byte, qa::protocol_header_bytes> &bytes, const std::size_t offset,
              const UInt value) {
    for (std::size_t index = 0; index < sizeof(UInt); ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

std::array<std::byte, qa::protocol_header_bytes> valid_header() {
    std::array<std::byte, qa::protocol_header_bytes> bytes{};
    bytes[0] = std::byte{0x41};
    bytes[1] = std::byte{0x59};
    bytes[2] = std::byte{0x51};
    bytes[3] = std::byte{0x41};
    write_le<std::uint16_t>(bytes, 4, 1);
    write_le<std::uint16_t>(bytes, 6, 0);
    write_le<std::uint16_t>(bytes, 8, 40);
    write_le<std::uint16_t>(bytes, 10, 2);
    write_le<std::uint32_t>(bytes, 16, 1);
    write_le<std::uint32_t>(bytes, 20, 1);
    write_le<std::uint64_t>(bytes, 24, 1);
    return bytes;
}

void require(const bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect_error(const std::array<std::byte, qa::protocol_header_bytes> &bytes,
                  const std::uint32_t channel, const qa::HeaderError expected,
                  const char *message) {
    require(qa::decode_protocol_header(bytes, channel).error == expected, message);
}

} // namespace

int main() {
    try {
        const auto valid = valid_header();
        const auto decoded = qa::decode_protocol_header(valid, 1);
        require(decoded.error == qa::HeaderError::none, "valid_header_rejected");
        require(decoded.header.type == qa::MessageType::request &&
                    decoded.header.payload_bytes == 1 && decoded.header.channel_id == 1 &&
                    decoded.header.channel_sequence == 1,
                "valid_header_fields_changed");

        require(qa::decode_protocol_header(std::span<const std::byte>{valid}.first(39), 1).error ==
                    qa::HeaderError::incomplete,
                "partial_header_accepted");

        auto changed = valid;
        changed[0] = std::byte{0};
        expect_error(changed, 1, qa::HeaderError::invalid_magic, "invalid_magic_accepted");

        for (const auto [offset, value] : std::array{std::pair{std::size_t{4}, std::uint16_t{2}},
                                                     std::pair{std::size_t{6}, std::uint16_t{1}}}) {
            changed = valid;
            write_le(changed, offset, value);
            expect_error(changed, 1, qa::HeaderError::incompatible_version,
                         "incompatible_version_accepted");
        }
        changed = valid;
        write_le<std::uint16_t>(changed, 8, 39);
        expect_error(changed, 1, qa::HeaderError::invalid_header_size,
                     "invalid_header_size_accepted");
        for (const auto type : {std::uint16_t{0}, std::uint16_t{10}}) {
            changed = valid;
            write_le(changed, 10, type);
            expect_error(changed, 1, qa::HeaderError::unknown_message_type,
                         "unknown_type_accepted");
        }
        changed = valid;
        write_le<std::uint32_t>(changed, 12, 1);
        expect_error(changed, 1, qa::HeaderError::unsupported_flags, "flags_accepted");
        for (const auto length : {std::uint32_t{0}, qa::max_protocol_payload_bytes + 1U}) {
            changed = valid;
            write_le(changed, 16, length);
            expect_error(changed, 1, qa::HeaderError::invalid_payload_length,
                         "invalid_length_accepted");
        }
        changed = valid;
        write_le(changed, 16, qa::max_protocol_payload_bytes);
        require(qa::decode_protocol_header(changed, 1).error == qa::HeaderError::none,
                "inclusive_payload_limit_rejected");

        changed = valid;
        write_le<std::uint32_t>(changed, 20, 2);
        expect_error(changed, 1, qa::HeaderError::invalid_channel, "wrong_channel_accepted");
        expect_error(valid, 0, qa::HeaderError::invalid_channel,
                     "invalid_expected_channel_accepted");
        changed = valid;
        write_le<std::uint16_t>(changed, 10, 1);
        expect_error(changed, 1, qa::HeaderError::invalid_channel, "wrong_type_direction_accepted");
        changed = valid;
        write_le<std::uint64_t>(changed, 24, 0);
        expect_error(changed, 1, qa::HeaderError::invalid_sequence, "zero_sequence_accepted");
        changed = valid;
        write_le<std::uint64_t>(changed, 24, 0x0807060504030201ULL);
        const auto endian = qa::decode_protocol_header(changed, 1);
        require(endian.error == qa::HeaderError::none &&
                    endian.header.channel_sequence == 0x0807060504030201ULL,
                "little_endian_sequence_changed");
        changed = valid;
        write_le<std::uint64_t>(changed, 32, 1);
        expect_error(changed, 1, qa::HeaderError::nonzero_reserved, "reserved_bytes_accepted");
        std::puts("protocol_header_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "protocol_header_test: %s\n", error.what());
        return 1;
    }
}
