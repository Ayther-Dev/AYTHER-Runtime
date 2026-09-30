#include "control_message.h"

#include "model_toml.h"
#include "protocol_header.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

namespace ayther::audio_qa {
namespace {

EncodedControlMessage encode(const Request &request, const MessageType type,
                             const std::uint32_t channel, const std::uint64_t sequence) {
    if (sequence == 0 ||
        (type == MessageType::request && request.admission != Admission::pending) ||
        (type == MessageType::session_status && request.admission == Admission::pending)) {
        return ControlMessageError::invalid_model;
    }
    auto encoded = to_toml(request);
    const auto *payload = std::get_if<std::string>(&encoded);
    if (payload == nullptr || payload->empty() || payload->size() > max_protocol_payload_bytes) {
        return ControlMessageError::invalid_model;
    }
    const auto header = encode_protocol_header(
        {type, static_cast<std::uint32_t>(payload->size()), channel, sequence});
    std::vector<std::byte> message(header.begin(), header.end());
    message.reserve(header.size() + payload->size());
    std::transform(
        payload->begin(), payload->end(), std::back_inserter(message),
        [](const char value) { return static_cast<std::byte>(static_cast<unsigned char>(value)); });
    return message;
}

DecodedControlMessage decode(const std::span<const std::byte> message,
                             const MessageType expected_type, const std::uint32_t channel,
                             const std::uint64_t expected_sequence) {
    if (message.size() < protocol_header_bytes) {
        return ControlMessageError::header_rejected;
    }
    const auto decoded = decode_protocol_header(message.first(protocol_header_bytes), channel);
    if (decoded.error != HeaderError::none) {
        return ControlMessageError::header_rejected;
    }
    if (decoded.header.type != expected_type) {
        return ControlMessageError::wrong_message_type;
    }
    if (decoded.header.channel_sequence != expected_sequence) {
        return ControlMessageError::sequence_mismatch;
    }
    if (decoded.header.payload_bytes != message.size() - protocol_header_bytes) {
        return ControlMessageError::length_mismatch;
    }
    const auto payload = message.subspan(protocol_header_bytes);
    const std::string_view text{reinterpret_cast<const char *>(payload.data()), payload.size()};
    auto request = request_from_toml(text);
    if (const auto *value = std::get_if<Request>(&request)) {
        const bool valid_admission =
            (expected_type == MessageType::request && value->admission == Admission::pending) ||
            (expected_type == MessageType::session_status &&
             value->admission != Admission::pending);
        if (valid_admission) {
            return *value;
        }
    }
    return ControlMessageError::invalid_payload;
}

} // namespace

EncodedControlMessage encode_request_message(const Request &request, const std::uint64_t sequence) {
    return encode(request, MessageType::request, 1, sequence);
}

EncodedControlMessage encode_admission_message(const Request &request,
                                               const std::uint64_t sequence) {
    return encode(request, MessageType::session_status, 2, sequence);
}

DecodedControlMessage decode_request_message(const std::span<const std::byte> message,
                                             const std::uint64_t expected_sequence) {
    return decode(message, MessageType::request, 1, expected_sequence);
}

DecodedControlMessage decode_admission_message(const std::span<const std::byte> message,
                                               const std::uint64_t expected_sequence) {
    return decode(message, MessageType::session_status, 2, expected_sequence);
}

} // namespace ayther::audio_qa
