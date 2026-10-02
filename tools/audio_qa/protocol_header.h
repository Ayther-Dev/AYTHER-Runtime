#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ayther::audio_qa {

inline constexpr std::size_t protocol_header_bytes = 40;
inline constexpr std::uint32_t max_protocol_payload_bytes = 256U * 1024U - protocol_header_bytes;

enum class MessageType : std::uint16_t {
    capabilities = 1,
    request = 2,
    cancel = 3,
    session_status = 4,
    fact_batch = 5,
    audio_chunk = 6,
    terminal = 7,
    diagnostic = 8,
    acknowledgement = 9,
};

struct ProtocolHeader {
    MessageType type{};
    std::uint32_t payload_bytes{};
    std::uint32_t channel_id{};
    std::uint64_t channel_sequence{};
};

enum class HeaderError {
    none,
    incomplete,
    invalid_magic,
    incompatible_version,
    invalid_header_size,
    unknown_message_type,
    unsupported_flags,
    invalid_payload_length,
    invalid_channel,
    invalid_sequence,
    nonzero_reserved,
};

struct HeaderDecodeResult {
    HeaderError error{HeaderError::none};
    ProtocolHeader header{};
};

[[nodiscard]] HeaderDecodeResult decode_protocol_header(std::span<const std::byte> bytes,
                                                        std::uint32_t expected_channel) noexcept;

[[nodiscard]] std::array<std::byte, protocol_header_bytes>
encode_protocol_header(const ProtocolHeader &header) noexcept;

} // namespace ayther::audio_qa
