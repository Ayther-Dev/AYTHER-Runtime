#include "cancellation_message.h"

#include "protocol_header.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <sstream>
#include <string_view>
#include <toml++/toml.hpp>

namespace ayther::audio_qa {
namespace {

constexpr std::array<std::string_view, 4> stage_names{"requested", "acknowledged", "applied",
                                                      "cessation_confirmed"};

MessageType message_type(const CancellationStage stage) noexcept {
    switch (stage) {
    case CancellationStage::requested:
        return MessageType::cancel;
    case CancellationStage::acknowledged:
        return MessageType::acknowledgement;
    case CancellationStage::applied:
        return MessageType::session_status;
    case CancellationStage::cessation_confirmed:
        return MessageType::terminal;
    }
    return MessageType::diagnostic;
}

std::uint32_t channel(const CancellationStage stage) noexcept {
    return stage == CancellationStage::requested || stage == CancellationStage::acknowledged ? 1U
                                                                                             : 2U;
}

bool valid_identity(const std::string &value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}

bool valid(const CancellationMessage &message) noexcept {
    if (!valid_identity(message.request_id) || !valid_identity(message.run_id)) {
        return false;
    }
    if (message.stage == CancellationStage::requested ||
        message.stage == CancellationStage::acknowledged) {
        return message.frames_completed == 0 && !message.last_engine_frame.has_value();
    }
    return message.frames_completed == 0 ? !message.last_engine_frame.has_value()
                                         : message.last_engine_frame.has_value();
}

std::optional<std::uint64_t> nonnegative(const toml::node_view<const toml::node> node) {
    const auto value = node.value<std::int64_t>();
    if (!value.has_value() || *value < 0) {
        return {};
    }
    return static_cast<std::uint64_t>(*value);
}

} // namespace

EncodedCancellationMessage encode_cancellation_message(const CancellationMessage &message,
                                                       const std::uint64_t sequence) {
    if (sequence == 0 || !valid(message)) {
        return CancellationMessageError::invalid_model;
    }
    toml::table table{{"request_id", message.request_id},
                      {"run_id", message.run_id},
                      {"stage", stage_names[static_cast<std::size_t>(message.stage)]},
                      {"frames_completed", static_cast<std::int64_t>(message.frames_completed)}};
    if (message.last_engine_frame.has_value()) {
        if (*message.last_engine_frame >
            static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
            return CancellationMessageError::invalid_model;
        }
        table.insert("last_engine_frame", static_cast<std::int64_t>(*message.last_engine_frame));
    }
    std::ostringstream output;
    output << table;
    const auto payload = output.str();
    if (payload.empty() || payload.size() > max_protocol_payload_bytes) {
        return CancellationMessageError::invalid_model;
    }
    const auto header = encode_protocol_header({message_type(message.stage),
                                                static_cast<std::uint32_t>(payload.size()),
                                                channel(message.stage), sequence});
    std::vector<std::byte> result(header.begin(), header.end());
    result.reserve(header.size() + payload.size());
    std::transform(
        payload.begin(), payload.end(), std::back_inserter(result),
        [](const char value) { return static_cast<std::byte>(static_cast<unsigned char>(value)); });
    return result;
}

DecodedCancellationMessage decode_cancellation_message(const std::span<const std::byte> bytes,
                                                       const CancellationStage expected_stage,
                                                       const std::uint64_t expected_sequence) {
    if (bytes.size() < protocol_header_bytes) {
        return CancellationMessageError::header_rejected;
    }
    const auto decoded =
        decode_protocol_header(bytes.first(protocol_header_bytes), channel(expected_stage));
    if (decoded.error != HeaderError::none) {
        return CancellationMessageError::header_rejected;
    }
    if (decoded.header.type != message_type(expected_stage)) {
        return CancellationMessageError::wrong_message_type;
    }
    if (decoded.header.channel_sequence != expected_sequence) {
        return CancellationMessageError::sequence_mismatch;
    }
    if (decoded.header.payload_bytes != bytes.size() - protocol_header_bytes) {
        return CancellationMessageError::length_mismatch;
    }
    try {
        const auto payload = bytes.subspan(protocol_header_bytes);
        const std::string_view text{reinterpret_cast<const char *>(payload.data()), payload.size()};
        const auto table = toml::parse(text);
        const auto request_id = table["request_id"].value<std::string>();
        const auto run_id = table["run_id"].value<std::string>();
        const auto stage = table["stage"].value<std::string_view>();
        const auto frames = nonnegative(table["frames_completed"]);
        const bool has_engine_frame = table.contains("last_engine_frame");
        const auto engine_frame = has_engine_frame ? nonnegative(table["last_engine_frame"])
                                                   : std::optional<std::uint64_t>{};
        const auto expected_fields = has_engine_frame ? 5U : 4U;
        if (!request_id.has_value() || !run_id.has_value() || !stage.has_value() ||
            !frames.has_value() || *frames > (std::numeric_limits<std::uint32_t>::max)() ||
            (has_engine_frame && !engine_frame.has_value()) || table.size() != expected_fields ||
            *stage != stage_names[static_cast<std::size_t>(expected_stage)]) {
            return CancellationMessageError::invalid_payload;
        }
        CancellationMessage message{*request_id, *run_id, expected_stage,
                                    static_cast<std::uint32_t>(*frames), engine_frame};
        return valid(message)
                   ? DecodedCancellationMessage{std::move(message)}
                   : DecodedCancellationMessage{CancellationMessageError::invalid_payload};
    } catch (const toml::parse_error &) {
        return CancellationMessageError::invalid_payload;
    }
}

} // namespace ayther::audio_qa
