#include "fact_batch.h"
#include "protocol_header.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {
template <typename UInt>
UInt read_le(const std::vector<std::byte> &bytes, const std::size_t offset) {
    UInt value{};
    for (std::size_t index = 0; index < sizeof(UInt); ++index) {
        value |= static_cast<UInt>(std::to_integer<unsigned>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

template <typename UInt>
void write_le(std::vector<std::byte> &bytes, const std::size_t offset, const UInt value) {
    for (std::size_t index = 0; index < sizeof(UInt); ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

void require(const bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

qa::Fact sample_fact(const std::uint64_t sequence, const char *kind) {
    qa::Fact fact;
    fact.id = {"run-92", "selector", sequence};
    fact.kind = kind;
    fact.frame_index = {qa::Availability::known, 42, {}};
    fact.cause_ids = {qa::FactId{"run-92", "detector", 7}, qa::PreexistingContext{"initial-state"}};
    fact.decision_id = {qa::Availability::known, "decision-8", {}};
    fact.assignment_id = {qa::Availability::known, "assignment-3", {}};
    fact.occurrence_id = {qa::Availability::known, "occurrence-5", {}};
    fact.reason_code = {qa::Availability::known, "selected", {}};
    fact.shared_state_order = {qa::Availability::known,
                               std::vector<qa::SharedStateOrder>{{"selection", 11}, {"voices", 4}},
                               {}};
    fact.fields = {
        {"mix_begin",
         qa::Availability::known,
         qa::FactFieldUnit::sample_frame,
         std::uint64_t{1024},
         {}},
        {"gain", qa::Availability::known, qa::FactFieldUnit::linear_gain, 0.75, {}},
        {"label", qa::Availability::known, qa::FactFieldUnit::none, std::string{"music"}, {}},
        {"selection",
         qa::Availability::known,
         qa::FactFieldUnit::none,
         qa::FactId{"run-92", "selector", 7},
         {}},
        {"output_position", qa::Availability::unknown, qa::FactFieldUnit::sample_frame,
         std::monostate{}, "frame_timeline_only"},
    };
    return fact;
}

qa::Fact compact_fact() {
    qa::Fact fact;
    fact.id = {"r", "p", 1};
    fact.kind = "compact";
    fact.frame_index = {qa::Availability::known, 1, {}};
    fact.decision_id = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    fact.assignment_id = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    fact.occurrence_id = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    fact.reason_code = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    fact.shared_state_order = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    return fact;
}
} // namespace

int main() {
    try {
        const std::vector facts{sample_fact(8, "candidate_selected"),
                                sample_fact(9, "voice_started")};
        const auto encoded = qa::encode_fact_batch(facts, 31);
        const auto *bytes = std::get_if<std::vector<std::byte>>(&encoded);
        require(bytes != nullptr, "fact_batch_encoding_failed");
        constexpr std::size_t first_record =
            qa::protocol_header_bytes + sizeof(std::uint32_t) + sizeof(std::uint32_t);
        require(bytes->size() > first_record + 6U &&
                    std::to_integer<unsigned char>((*bytes)[first_record]) == 'A' &&
                    std::to_integer<unsigned char>((*bytes)[first_record + 1U]) == 'Q' &&
                    std::to_integer<unsigned char>((*bytes)[first_record + 2U]) == 'F' &&
                    std::to_integer<unsigned char>((*bytes)[first_record + 3U]) == '4' &&
                    std::to_integer<unsigned char>((*bytes)[first_record + 4U]) == 1U &&
                    std::to_integer<unsigned char>((*bytes)[first_record + 5U]) == 4U,
                "detailed_fact_did_not_use_binary_schema_1_4");
        const auto decoded = qa::decode_fact_batch(*bytes, 31);
        require(std::holds_alternative<std::vector<qa::Fact>>(decoded) &&
                    std::get<std::vector<qa::Fact>>(decoded) == facts,
                "fact_batch_round_trip_lost_relations");

        auto unknown_binary_minor = *bytes;
        unknown_binary_minor[first_record + 5U] = static_cast<std::byte>(5U);
        const auto unknown_binary_result = qa::decode_fact_batch(unknown_binary_minor, 31);
        require(std::holds_alternative<qa::FactBatchError>(unknown_binary_result) &&
                    std::get<qa::FactBatchError>(unknown_binary_result) ==
                        qa::FactBatchError::malformed_fact,
                "unknown_binary_fact_schema_minor_accepted");

        const std::vector compact_facts{compact_fact()};
        const auto compact_encoded = qa::encode_fact_batch(compact_facts, 32);
        const auto *compact_bytes = std::get_if<std::vector<std::byte>>(&compact_encoded);
        require(compact_bytes != nullptr, "compact_fact_encoding_failed");
        const auto compact_decoded = qa::decode_fact_batch(*compact_bytes, 32);
        require(std::holds_alternative<std::vector<qa::Fact>>(compact_decoded) &&
                    std::get<std::vector<qa::Fact>>(compact_decoded) == compact_facts,
                "compact_fact_round_trip_lost_defaults");

        auto unknown_minor = *compact_bytes;
        constexpr std::string_view minor_marker{"schema_minor = 1"};
        const auto marker =
            std::search(unknown_minor.begin(), unknown_minor.end(), minor_marker.begin(),
                        minor_marker.end(), [](const std::byte byte, const char character) {
                            return std::to_integer<unsigned char>(byte) ==
                                   static_cast<unsigned char>(character);
                        });
        require(marker != unknown_minor.end(), "compact_schema_minor_marker_missing");
        *(marker + static_cast<std::ptrdiff_t>(minor_marker.size() - 1U)) =
            static_cast<std::byte>('2');
        const auto unknown_minor_result = qa::decode_fact_batch(unknown_minor, 32);
        require(std::holds_alternative<qa::FactBatchError>(unknown_minor_result) &&
                    std::get<qa::FactBatchError>(unknown_minor_result) ==
                        qa::FactBatchError::malformed_fact,
                "unknown_fact_schema_minor_accepted");

        auto truncated = *bytes;
        const auto first_length = read_le<std::uint32_t>(truncated, 44);
        const std::size_t second_length_at = 48U + first_length;
        const auto second_length = read_le<std::uint32_t>(truncated, second_length_at);
        truncated.pop_back();
        write_le<std::uint32_t>(truncated, 16, static_cast<std::uint32_t>(truncated.size() - 40U));
        write_le<std::uint32_t>(truncated, second_length_at, second_length - 1U);
        const auto truncated_result = qa::decode_fact_batch(truncated, 31);
        require(std::holds_alternative<qa::FactBatchError>(truncated_result) &&
                    std::get<qa::FactBatchError>(truncated_result) ==
                        qa::FactBatchError::malformed_fact,
                "truncated_fact_text_accepted");

        auto truncated_prefix = *bytes;
        truncated_prefix.resize(46);
        write_le<std::uint32_t>(truncated_prefix, 16, 6);
        const auto prefix_result = qa::decode_fact_batch(truncated_prefix, 31);
        require(std::holds_alternative<qa::FactBatchError>(prefix_result) &&
                    std::get<qa::FactBatchError>(prefix_result) == qa::FactBatchError::truncated,
                "truncated_record_prefix_accepted");

        auto excessive_count = *bytes;
        write_le<std::uint32_t>(excessive_count, 40, 1025);
        const auto count_result = qa::decode_fact_batch(excessive_count, 31);
        require(std::holds_alternative<qa::FactBatchError>(count_result) &&
                    std::get<qa::FactBatchError>(count_result) == qa::FactBatchError::invalid_count,
                "excessive_record_count_accepted");
        require(std::holds_alternative<qa::FactBatchError>(qa::decode_fact_batch(*bytes, 32)),
                "fact_batch_sequence_mismatch_accepted");
        require(std::holds_alternative<qa::FactBatchError>(qa::encode_fact_batch({}, 1)),
                "empty_fact_batch_encoded");

        std::puts("fact_batch_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "fact_batch_test: %s\n", error.what());
        return 1;
    }
}
