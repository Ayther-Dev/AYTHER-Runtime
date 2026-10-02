#pragma once

#include "model_limits.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

enum class CancellationStage {
    requested,
    acknowledged,
    applied,
    cessation_confirmed,
};

struct CancellationMessage {
    std::string request_id;
    std::string run_id;
    CancellationStage stage{CancellationStage::requested};
    std::uint32_t frames_completed{};
    std::optional<std::uint64_t> last_engine_frame;
    bool operator==(const CancellationMessage &) const = default;
};

enum class CancellationMessageError {
    invalid_model,
    header_rejected,
    wrong_message_type,
    sequence_mismatch,
    length_mismatch,
    invalid_payload,
};

using EncodedCancellationMessage = std::variant<std::vector<std::byte>, CancellationMessageError>;
using DecodedCancellationMessage = std::variant<CancellationMessage, CancellationMessageError>;

[[nodiscard]] EncodedCancellationMessage
encode_cancellation_message(const CancellationMessage &message, std::uint64_t sequence);

[[nodiscard]] DecodedCancellationMessage
decode_cancellation_message(std::span<const std::byte> bytes, CancellationStage expected_stage,
                            std::uint64_t expected_sequence);

} // namespace ayther::audio_qa
