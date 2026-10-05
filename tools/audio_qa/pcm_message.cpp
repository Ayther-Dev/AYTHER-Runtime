#include "pcm_message.h"

#include "content_hash.h"
#include "protocol_header.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

namespace ayther::audio_qa {
namespace {

constexpr std::array format_names{"s16le", "s24le", "s32le", "f32le"};
constexpr std::array availability_names{"known", "not_applicable", "unknown"};
constexpr std::array discontinuity_names{"inserted_silence", "discarded_audio"};
constexpr char hex_digits[] = "0123456789abcdef";

template <typename UInt> void append_le(std::vector<std::byte> &bytes, const UInt value) {
    for (std::size_t index = 0; index < sizeof(UInt); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
    }
}

template <typename UInt>
UInt read_le(const std::span<const std::byte> bytes, const std::size_t offset) noexcept {
    UInt value{};
    for (std::size_t index = 0; index < sizeof(UInt); ++index) {
        value |= static_cast<UInt>(std::to_integer<unsigned>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

std::string hex(const std::array<std::uint8_t, 32> &value) {
    std::string result(64, '0');
    for (std::size_t index = 0; index < value.size(); ++index) {
        result[index * 2] = hex_digits[value[index] >> 4U];
        result[index * 2 + 1] = hex_digits[value[index] & 0x0fU];
    }
    return result;
}

std::array<std::uint8_t, 32> parse_hex(const std::string_view text) {
    if (text.size() != 64) {
        throw PcmMessageError::malformed_metadata;
    }
    std::array<std::uint8_t, 32> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        unsigned value{};
        const auto parsed =
            std::from_chars(text.data() + index * 2, text.data() + index * 2 + 2, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + index * 2 + 2 || value > 255U) {
            throw PcmMessageError::malformed_metadata;
        }
        result[index] = static_cast<std::uint8_t>(value);
    }
    return result;
}

std::uint64_t decimal(const toml::node_view<const toml::node> node) {
    const auto text = node.value<std::string>();
    if (!text || text->empty() || text->front() == '+' || text->front() == '-') {
        throw PcmMessageError::malformed_metadata;
    }
    std::uint64_t value{};
    const auto parsed = std::from_chars(text->data(), text->data() + text->size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size()) {
        throw PcmMessageError::malformed_metadata;
    }
    return value;
}

const char *availability_name(const Availability value) {
    const auto index = static_cast<std::size_t>(value);
    if (index >= availability_names.size()) {
        throw PcmMessageError::invalid_chunk;
    }
    return availability_names[index];
}

Availability availability_value(const toml::table &table) {
    const auto text = table["availability"].value<std::string>();
    if (!text) {
        throw PcmMessageError::malformed_metadata;
    }
    for (std::size_t index = 0; index < availability_names.size(); ++index) {
        if (*text == availability_names[index]) {
            return static_cast<Availability>(index);
        }
    }
    throw PcmMessageError::malformed_metadata;
}

toml::table range_table(const SampleFrameRange &range) {
    return toml::table{{"timeline", range.timeline_id},
                       {"sample_rate", static_cast<std::int64_t>(range.sample_rate)},
                       {"begin", std::to_string(range.begin)},
                       {"end", std::to_string(range.end)}};
}

SampleFrameRange parse_range(const toml::node_view<const toml::node> node) {
    const auto *table = node.as_table();
    const auto timeline = table ? (*table)["timeline"].value<std::string>() : std::nullopt;
    const auto rate = table ? (*table)["sample_rate"].value<std::int64_t>() : std::nullopt;
    if (!table || !timeline || !rate || *rate < 0 ||
        *rate > std::numeric_limits<std::uint32_t>::max()) {
        throw PcmMessageError::malformed_metadata;
    }
    return {*timeline, static_cast<std::uint32_t>(*rate), decimal((*table)["begin"]),
            decimal((*table)["end"])};
}

toml::table range_field(const Field<SampleFrameRange> &field) {
    toml::table table{{"availability", availability_name(field.availability)},
                      {"reason_code", field.reason_code}};
    if (field.value) {
        table.insert("value", range_table(*field.value));
    }
    return table;
}

Field<SampleFrameRange> parse_range_field(const toml::node_view<const toml::node> node) {
    const auto *table = node.as_table();
    if (!table) {
        throw PcmMessageError::malformed_metadata;
    }
    Field<SampleFrameRange> field;
    field.availability = availability_value(*table);
    field.reason_code = table->get_as<std::string>("reason_code")
                            ? table->get_as<std::string>("reason_code")->get()
                            : std::string{};
    if (table->contains("value")) {
        field.value = parse_range((*table)["value"]);
    }
    return field;
}

toml::array discontinuities_table(const AudioChunk &chunk) {
    toml::array result;
    for (const auto &discontinuity : chunk.discontinuities) {
        const auto kind_index = static_cast<std::size_t>(discontinuity.kind);
        if (kind_index >= discontinuity_names.size()) {
            throw PcmMessageError::invalid_chunk;
        }
        toml::array causes;
        for (const auto &cause : discontinuity.cause_ids) {
            causes.push_back(
                toml::table{{"run_id", cause.run_id},
                            {"producer_id", cause.producer_id},
                            {"producer_sequence", std::to_string(cause.producer_sequence)}});
        }
        result.push_back(toml::table{{"kind", discontinuity_names[kind_index]},
                                     {"affected_range", range_table(discontinuity.affected_range)},
                                     {"output_range", range_field(discontinuity.output_range)},
                                     {"cause_ids", std::move(causes)}});
    }
    return result;
}

std::vector<AudioDiscontinuity>
parse_discontinuities(const toml::node_view<const toml::node> node) {
    const auto *array = node.as_array();
    if (!array || array->size() > max_audio_discontinuities) {
        throw PcmMessageError::malformed_metadata;
    }
    std::vector<AudioDiscontinuity> result;
    result.reserve(array->size());
    for (const auto &item : *array) {
        const auto *table = item.as_table();
        const auto kind = table ? (*table)["kind"].value<std::string>() : std::nullopt;
        const auto *causes = table ? (*table)["cause_ids"].as_array() : nullptr;
        if (!table || !kind || !causes || causes->size() > max_fact_causes) {
            throw PcmMessageError::malformed_metadata;
        }
        const auto kind_at =
            std::find(discontinuity_names.begin(), discontinuity_names.end(), *kind);
        if (kind_at == discontinuity_names.end()) {
            throw PcmMessageError::malformed_metadata;
        }
        AudioDiscontinuity discontinuity;
        discontinuity.kind = static_cast<DiscontinuityKind>(kind_at - discontinuity_names.begin());
        discontinuity.affected_range = parse_range((*table)["affected_range"]);
        discontinuity.output_range = parse_range_field((*table)["output_range"]);
        for (const auto &cause_node : *causes) {
            const auto *cause = cause_node.as_table();
            const auto run = cause ? (*cause)["run_id"].value<std::string>() : std::nullopt;
            const auto producer =
                cause ? (*cause)["producer_id"].value<std::string>() : std::nullopt;
            if (!cause || !run || !producer) {
                throw PcmMessageError::malformed_metadata;
            }
            discontinuity.cause_ids.push_back(
                {*run, *producer, decimal((*cause)["producer_sequence"])});
        }
        result.push_back(std::move(discontinuity));
    }
    return result;
}

toml::array causes_table(const std::vector<FactId> &causes) {
    toml::array result;
    for (const auto &cause : causes) {
        result.push_back(
            toml::table{{"run_id", cause.run_id},
                        {"producer_id", cause.producer_id},
                        {"producer_sequence", std::to_string(cause.producer_sequence)}});
    }
    return result;
}

std::vector<FactId> parse_causes(const toml::node_view<const toml::node> node) {
    const auto *array = node.as_array();
    if (!array || array->size() > max_fact_causes)
        throw PcmMessageError::malformed_metadata;
    std::vector<FactId> result;
    result.reserve(array->size());
    for (const auto &item : *array) {
        const auto *table = item.as_table();
        const auto run = table ? (*table)["run_id"].value<std::string>() : std::nullopt;
        const auto producer = table ? (*table)["producer_id"].value<std::string>() : std::nullopt;
        if (!table || !run || !producer)
            throw PcmMessageError::malformed_metadata;
        result.push_back({*run, *producer, decimal((*table)["producer_sequence"])});
    }
    return result;
}

toml::table metadata(const AudioChunk &chunk) {
    toml::table checkpoint{{"availability", availability_name(chunk.checkpoint_id.availability)},
                           {"reason_code", chunk.checkpoint_id.reason_code}};
    if (chunk.checkpoint_id.value) {
        checkpoint.insert("value", *chunk.checkpoint_id.value);
    }
    // Minor 1 adds the causes; minor 2 (spec 002, DI-14) adds the segment of the traversal.
    // A chunk of segment 0 keeps the minor it had, so that a linear take reads as before.
    const std::int64_t minor = chunk.segment != 0U ? 2 : (chunk.cause_ids.empty() ? 0 : 1);
    auto table = toml::table{
        {"schema_version", 1},
        {"schema_minor", minor},
        {"run_id", chunk.run_id},
        {"capture_point", chunk.capture_point},
        {"producer_sequence", std::to_string(chunk.producer_sequence)},
        {"pcm_format", format_names[static_cast<std::size_t>(chunk.format.pcm)]},
        {"sample_rate", static_cast<std::int64_t>(chunk.format.sample_rate)},
        {"channels", static_cast<std::int64_t>(chunk.format.channels)},
        {"timeline", chunk.range.timeline_id},
        {"sample_begin", std::to_string(chunk.range.begin)},
        {"sample_count", std::to_string(chunk.range.end - chunk.range.begin)},
        {"sha256", hex(*chunk.sha256.value)},
        {"durability", chunk.durability == Durability::confirmed ? "confirmed" : "pending"},
        {"checkpoint", std::move(checkpoint)},
        {"causes", causes_table(chunk.cause_ids)},
        {"discontinuities", discontinuities_table(chunk)}};
    if (chunk.segment != 0U)
        table.insert("segment", std::to_string(chunk.segment));
    return table;
}

AudioChunk parse_metadata(const std::string_view text) {
    try {
        const auto table = toml::parse(text);
        const auto schema_minor = table["schema_minor"].value<std::int64_t>();
        if (table["schema_version"].value<std::int64_t>() != 1 || !schema_minor ||
            *schema_minor < 0 || *schema_minor > 2) {
            throw PcmMessageError::malformed_metadata;
        }
        AudioChunk chunk;
        const auto run = table["run_id"].value<std::string>();
        const auto capture = table["capture_point"].value<std::string>();
        const auto format = table["pcm_format"].value<std::string>();
        const auto rate = table["sample_rate"].value<std::int64_t>();
        const auto channels = table["channels"].value<std::int64_t>();
        const auto timeline = table["timeline"].value<std::string>();
        if (!run || !capture || !format || !rate || !channels || !timeline || *rate < 0 ||
            *rate > std::numeric_limits<std::uint32_t>::max() || *channels < 0 ||
            *channels > std::numeric_limits<std::uint8_t>::max()) {
            throw PcmMessageError::malformed_metadata;
        }
        chunk.run_id = *run;
        chunk.capture_point = *capture;
        chunk.producer_sequence = decimal(table["producer_sequence"]);
        const auto format_at = std::find(format_names.begin(), format_names.end(), *format);
        if (format_at == format_names.end()) {
            throw PcmMessageError::malformed_metadata;
        }
        chunk.format = {static_cast<PcmFormat>(format_at - format_names.begin()),
                        static_cast<std::uint32_t>(*rate), static_cast<std::uint8_t>(*channels)};
        const auto begin = decimal(table["sample_begin"]);
        const auto count = decimal(table["sample_count"]);
        if (count == 0 || begin > std::numeric_limits<std::uint64_t>::max() - count) {
            throw PcmMessageError::malformed_metadata;
        }
        chunk.range = {*timeline, chunk.format.sample_rate, begin, begin + count};
        const auto digest = table["sha256"].value<std::string>();
        if (!digest) {
            throw PcmMessageError::hash_required;
        }
        chunk.sha256 = {Availability::known, parse_hex(*digest), {}};
        const auto *checkpoint = table["checkpoint"].as_table();
        const auto durability = table["durability"].value<std::string>();
        if (!checkpoint || !durability) {
            throw PcmMessageError::malformed_metadata;
        }
        chunk.checkpoint_id.availability = availability_value(*checkpoint);
        chunk.checkpoint_id.reason_code =
            checkpoint->get_as<std::string>("reason_code")
                ? checkpoint->get_as<std::string>("reason_code")->get()
                : std::string{};
        if (const auto value = (*checkpoint)["value"].value<std::string>()) {
            chunk.checkpoint_id.value = *value;
        }
        if (*durability == "pending") {
            chunk.durability = Durability::pending;
        } else if (*durability == "confirmed") {
            chunk.durability = Durability::confirmed;
        } else {
            throw PcmMessageError::malformed_metadata;
        }
        chunk.discontinuities = parse_discontinuities(table["discontinuities"]);
        const auto *causes = table["causes"].as_array();
        if (*schema_minor >= 1 && causes == nullptr)
            throw PcmMessageError::malformed_metadata;
        if (causes != nullptr)
            chunk.cause_ids = parse_causes(table["causes"]);
        // DI-14: only minor 2 names a segment, and never segment 0.
        if (*schema_minor == 2) {
            chunk.segment = decimal(table["segment"]);
            if (chunk.segment == 0U)
                throw PcmMessageError::malformed_metadata;
        } else if (table.contains("segment")) {
            throw PcmMessageError::malformed_metadata;
        }
        return chunk;
    } catch (const toml::parse_error &) {
        throw PcmMessageError::malformed_metadata;
    }
}

} // namespace

std::array<std::uint8_t, 32> pcm_sha256(const std::span<const std::byte> bytes) noexcept {
    return identify_content(bytes).sha256;
}

EncodedPcmMessage encode_pcm_message(const AudioChunk &chunk,
                                     const std::uint64_t channel_sequence) {
    try {
        if (!well_formed(chunk) || channel_sequence == 0 || chunk.range.begin == chunk.range.end) {
            return PcmMessageError::invalid_chunk;
        }
        if (chunk.sha256.availability != Availability::known || !chunk.sha256.value) {
            return PcmMessageError::hash_required;
        }
        if (*chunk.sha256.value != pcm_sha256(chunk.bytes)) {
            return PcmMessageError::hash_mismatch;
        }
        std::ostringstream stream;
        stream << metadata(chunk);
        const std::string text = stream.str();
        if (text.empty() || text.size() > max_protocol_payload_bytes) {
            return PcmMessageError::metadata_too_large;
        }
        const std::size_t payload_size = 8U + text.size() + chunk.bytes.size();
        if (payload_size > max_protocol_payload_bytes) {
            return PcmMessageError::message_too_large;
        }
        std::vector<std::byte> payload;
        payload.reserve(payload_size);
        append_le(payload, static_cast<std::uint32_t>(text.size()));
        append_le(payload, static_cast<std::uint32_t>(chunk.bytes.size()));
        for (const unsigned char byte : text) {
            payload.push_back(static_cast<std::byte>(byte));
        }
        payload.insert(payload.end(), chunk.bytes.begin(), chunk.bytes.end());
        const auto header = encode_protocol_header({MessageType::audio_chunk,
                                                    static_cast<std::uint32_t>(payload.size()), 2,
                                                    channel_sequence});
        std::vector<std::byte> message(header.begin(), header.end());
        message.insert(message.end(), payload.begin(), payload.end());
        return message;
    } catch (const PcmMessageError error) {
        return error;
    }
}

DecodedPcmMessage decode_pcm_message(const std::span<const std::byte> message,
                                     const std::uint64_t expected_sequence) {
    if (message.size() < protocol_header_bytes) {
        return PcmMessageError::header_rejected;
    }
    const auto header = decode_protocol_header(message.first(protocol_header_bytes), 2);
    if (header.error != HeaderError::none) {
        return PcmMessageError::header_rejected;
    }
    if (header.header.type != MessageType::audio_chunk) {
        return PcmMessageError::wrong_message_type;
    }
    if (header.header.channel_sequence != expected_sequence) {
        return PcmMessageError::sequence_mismatch;
    }
    if (header.header.payload_bytes != message.size() - protocol_header_bytes) {
        return PcmMessageError::length_mismatch;
    }
    const auto payload = message.subspan(protocol_header_bytes);
    if (payload.size() < 8U) {
        return PcmMessageError::truncated;
    }
    const auto metadata_bytes = read_le<std::uint32_t>(payload, 0);
    const auto pcm_bytes = read_le<std::uint32_t>(payload, 4);
    if (metadata_bytes == 0 || pcm_bytes == 0 || metadata_bytes > payload.size() - 8U ||
        pcm_bytes != payload.size() - 8U - metadata_bytes) {
        return PcmMessageError::length_mismatch;
    }
    try {
        const auto metadata_span = payload.subspan(8, metadata_bytes);
        AudioChunk chunk = parse_metadata(std::string_view{
            reinterpret_cast<const char *>(metadata_span.data()), metadata_span.size()});
        const auto expected =
            expected_payload_bytes(chunk.format, chunk.range.end - chunk.range.begin);
        if (!expected || *expected != pcm_bytes) {
            return PcmMessageError::pcm_length_mismatch;
        }
        const auto pcm = payload.subspan(8U + metadata_bytes, pcm_bytes);
        chunk.bytes.assign(pcm.begin(), pcm.end());
        if (chunk.sha256.availability != Availability::known || !chunk.sha256.value) {
            return PcmMessageError::hash_required;
        }
        if (*chunk.sha256.value != pcm_sha256(chunk.bytes)) {
            return PcmMessageError::hash_mismatch;
        }
        if (!well_formed(chunk)) {
            return PcmMessageError::malformed_metadata;
        }
        return chunk;
    } catch (const PcmMessageError error) {
        return error;
    }
}

} // namespace ayther::audio_qa
