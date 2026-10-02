#pragma once

#include "inherited_channel.h"

#include <cstdint>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

enum class ProtocolIoError {
    none,
    read_failed,
    unexpected_end,
    header_rejected,
    allocation_failed,
};

using ProtocolReadResult = std::variant<std::vector<std::byte>, ProtocolIoError>;

[[nodiscard]] ProtocolReadResult read_protocol_message(OwnedChannelHandle &channel,
                                                       std::uint32_t expected_channel) noexcept;

} // namespace ayther::audio_qa
