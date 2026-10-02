#pragma once

#include "model.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

enum class ControlMessageError {
    invalid_model,
    header_rejected,
    wrong_message_type,
    sequence_mismatch,
    length_mismatch,
    invalid_payload,
};

using EncodedControlMessage = std::variant<std::vector<std::byte>, ControlMessageError>;
using DecodedControlMessage = std::variant<Request, ControlMessageError>;

[[nodiscard]] EncodedControlMessage encode_request_message(const Request &request,
                                                           std::uint64_t sequence);
[[nodiscard]] EncodedControlMessage encode_admission_message(const Request &request,
                                                             std::uint64_t sequence);
[[nodiscard]] DecodedControlMessage decode_request_message(std::span<const std::byte> message,
                                                           std::uint64_t expected_sequence);
[[nodiscard]] DecodedControlMessage decode_admission_message(std::span<const std::byte> message,
                                                             std::uint64_t expected_sequence);

} // namespace ayther::audio_qa
