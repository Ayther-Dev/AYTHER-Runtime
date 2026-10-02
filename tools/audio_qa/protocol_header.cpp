#include "protocol_header.h"

#include <array>

namespace ayther::audio_qa {
namespace {

template <typename UInt>
[[nodiscard]] UInt read_le(const std::span<const std::byte> bytes,
                           const std::size_t offset) noexcept {
    UInt value{};
    for (std::size_t index = 0; index < sizeof(UInt); ++index) {
        value |= static_cast<UInt>(std::to_integer<unsigned>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

template <typename UInt>
void write_le(std::span<std::byte> bytes, const std::size_t offset, const UInt value) noexcept {
    for (std::size_t index = 0; index < sizeof(UInt); ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

[[nodiscard]] bool known_type(const std::uint16_t type) noexcept {
    return type >= static_cast<std::uint16_t>(MessageType::capabilities) &&
           type <= static_cast<std::uint16_t>(MessageType::acknowledgement);
}

[[nodiscard]] std::uint32_t channel_for(const MessageType type) noexcept {
    return type == MessageType::request || type == MessageType::cancel ||
                   type == MessageType::acknowledgement
               ? 1U
               : 2U;
}

} // namespace

HeaderDecodeResult decode_protocol_header(const std::span<const std::byte> bytes,
                                          const std::uint32_t expected_channel) noexcept {
    if (bytes.size() < protocol_header_bytes) {
        return {HeaderError::incomplete, {}};
    }
    constexpr std::array magic{std::byte{0x41}, std::byte{0x59}, std::byte{0x51}, std::byte{0x41}};
    for (std::size_t index = 0; index < magic.size(); ++index) {
        if (bytes[index] != magic[index]) {
            return {HeaderError::invalid_magic, {}};
        }
    }
    if (read_le<std::uint16_t>(bytes, 4) != 1U || read_le<std::uint16_t>(bytes, 6) != 0U) {
        return {HeaderError::incompatible_version, {}};
    }
    if (read_le<std::uint16_t>(bytes, 8) != protocol_header_bytes) {
        return {HeaderError::invalid_header_size, {}};
    }
    const auto raw_type = read_le<std::uint16_t>(bytes, 10);
    if (!known_type(raw_type)) {
        return {HeaderError::unknown_message_type, {}};
    }
    if (read_le<std::uint32_t>(bytes, 12) != 0U) {
        return {HeaderError::unsupported_flags, {}};
    }
    const auto payload_bytes = read_le<std::uint32_t>(bytes, 16);
    if (payload_bytes == 0U || payload_bytes > max_protocol_payload_bytes) {
        return {HeaderError::invalid_payload_length, {}};
    }
    const auto channel_id = read_le<std::uint32_t>(bytes, 20);
    const auto type = static_cast<MessageType>(raw_type);
    if ((expected_channel != 1U && expected_channel != 2U) || channel_id != expected_channel ||
        channel_id != channel_for(type)) {
        return {HeaderError::invalid_channel, {}};
    }
    const auto sequence = read_le<std::uint64_t>(bytes, 24);
    if (sequence == 0U) {
        return {HeaderError::invalid_sequence, {}};
    }
    if (read_le<std::uint64_t>(bytes, 32) != 0U) {
        return {HeaderError::nonzero_reserved, {}};
    }
    return {HeaderError::none, ProtocolHeader{type, payload_bytes, channel_id, sequence}};
}

std::array<std::byte, protocol_header_bytes>
encode_protocol_header(const ProtocolHeader &header) noexcept {
    std::array<std::byte, protocol_header_bytes> bytes{};
    bytes[0] = std::byte{0x41};
    bytes[1] = std::byte{0x59};
    bytes[2] = std::byte{0x51};
    bytes[3] = std::byte{0x41};
    write_le<std::uint16_t>(bytes, 4, 1);
    write_le<std::uint16_t>(bytes, 6, 0);
    write_le<std::uint16_t>(bytes, 8, static_cast<std::uint16_t>(protocol_header_bytes));
    write_le(bytes, 10, static_cast<std::uint16_t>(header.type));
    write_le<std::uint32_t>(bytes, 12, 0);
    write_le(bytes, 16, header.payload_bytes);
    write_le(bytes, 20, header.channel_id);
    write_le(bytes, 24, header.channel_sequence);
    write_le<std::uint64_t>(bytes, 32, 0);
    return bytes;
}

} // namespace ayther::audio_qa
