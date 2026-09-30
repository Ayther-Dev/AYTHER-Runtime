#include "protocol_io.h"

#include "protocol_header.h"

#include <array>
#include <new>
#include <span>

namespace ayther::audio_qa {
namespace {

[[nodiscard]] ProtocolIoError read_exact(OwnedChannelHandle &channel,
                                         const std::span<std::byte> destination,
                                         std::size_t &written) noexcept {
    written = 0;
    while (written < destination.size()) {
        std::size_t received{};
        const auto status = read_channel(channel, destination.subspan(written), received);
        if (status == ChannelReadResult::failed)
            return ProtocolIoError::read_failed;
        if (status == ChannelReadResult::end_of_stream || received == 0U)
            return ProtocolIoError::unexpected_end;
        written += received;
    }
    return ProtocolIoError::none;
}

} // namespace

ProtocolReadResult read_protocol_message(OwnedChannelHandle &channel,
                                         const std::uint32_t expected_channel) noexcept {
    std::array<std::byte, protocol_header_bytes> header_bytes{};
    std::size_t received{};
    const auto header_read = read_exact(channel, header_bytes, received);
    if (received != header_bytes.size())
        return header_read;
    const auto header = decode_protocol_header(header_bytes, expected_channel);
    if (header.error != HeaderError::none)
        return ProtocolIoError::header_rejected;
    try {
        std::vector<std::byte> message(header_bytes.begin(), header_bytes.end());
        message.resize(protocol_header_bytes + header.header.payload_bytes);
        std::size_t payload_received{};
        const auto payload_read = read_exact(
            channel, std::span{message}.subspan(protocol_header_bytes), payload_received);
        if (payload_received != header.header.payload_bytes)
            return payload_read;
        return message;
    } catch (const std::bad_alloc &) {
        return ProtocolIoError::allocation_failed;
    }
}

} // namespace ayther::audio_qa
