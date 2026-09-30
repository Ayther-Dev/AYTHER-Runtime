#pragma once

#include "fact_model.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::size_t max_fact_batch_records = 1024;
inline constexpr std::size_t max_encoded_fact_bytes = 64U * 1024U;

enum class FactBatchError {
    invalid_fact,
    invalid_count,
    fact_too_large,
    batch_too_large,
    header_rejected,
    wrong_message_type,
    sequence_mismatch,
    truncated,
    length_mismatch,
    malformed_fact,
};

using EncodedFactBatch = std::variant<std::vector<std::byte>, FactBatchError>;
using DecodedFactBatch = std::variant<std::vector<Fact>, FactBatchError>;

[[nodiscard]] EncodedFactBatch encode_fact_batch(const std::vector<Fact> &facts,
                                                 std::uint64_t sequence);
[[nodiscard]] DecodedFactBatch decode_fact_batch(std::span<const std::byte> message,
                                                 std::uint64_t expected_sequence);

} // namespace ayther::audio_qa
