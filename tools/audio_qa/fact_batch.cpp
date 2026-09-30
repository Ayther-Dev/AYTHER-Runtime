#include "fact_batch.h"

#include "protocol_header.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace ayther::audio_qa {
namespace {

constexpr std::array availability_names{"known", "not_applicable", "unknown"};
constexpr std::array unit_names{"none",  "emulation_frame", "sample_frame",      "bytes",
                                "count", "linear_gain",     "frames_per_second", "nanoseconds"};
constexpr std::string_view compact_default_reason{"compact_default"};
constexpr std::array binary_fact_magic{std::byte{'A'}, std::byte{'Q'}, std::byte{'F'},
                                       std::byte{'4'}};
constexpr std::uint8_t binary_fact_schema_major = 1U;
constexpr std::uint8_t binary_fact_schema_minor = 4U;

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

const char *availability_name(const Availability availability) {
    const auto index = static_cast<std::size_t>(availability);
    if (index >= availability_names.size()) {
        throw FactBatchError::invalid_fact;
    }
    return availability_names[index];
}

Availability availability_value(const toml::table &table) {
    const auto text = table["availability"].value<std::string>();
    if (!text) {
        throw FactBatchError::malformed_fact;
    }
    for (std::size_t index = 0; index < availability_names.size(); ++index) {
        if (*text == availability_names[index]) {
            return static_cast<Availability>(index);
        }
    }
    throw FactBatchError::malformed_fact;
}

std::uint64_t decimal(const toml::node_view<const toml::node> node) {
    const auto text = node.value<std::string>();
    if (!text || text->empty() || text->front() == '+' || text->front() == '-') {
        throw FactBatchError::malformed_fact;
    }
    std::uint64_t value{};
    const auto parsed = std::from_chars(text->data(), text->data() + text->size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size()) {
        throw FactBatchError::malformed_fact;
    }
    return value;
}

std::uint64_t decimal_text(const std::string_view text) {
    if (text.empty() || text.front() == '+' || text.front() == '-')
        throw FactBatchError::malformed_fact;
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw FactBatchError::malformed_fact;
    return value;
}

class BinaryFactWriter final {
  public:
    BinaryFactWriter() { bytes_.reserve(1024U); }

    void byte(const std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }

    template <typename UInt> void integer(const UInt value) {
        static_assert(std::is_unsigned_v<UInt>);
        append_le(bytes_, value);
    }

    void text(const std::string_view value) {
        if (value.size() > (std::numeric_limits<std::uint32_t>::max)())
            throw FactBatchError::invalid_fact;
        integer(static_cast<std::uint32_t>(value.size()));
        const auto previous = bytes_.size();
        bytes_.resize(previous + value.size());
        if (!value.empty())
            std::memcpy(bytes_.data() + previous, value.data(), value.size());
    }

    [[nodiscard]] std::vector<std::byte> finish() && { return std::move(bytes_); }

  private:
    std::vector<std::byte> bytes_;
};

class BinaryFactReader final {
  public:
    explicit BinaryFactReader(const std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] std::uint8_t byte() {
        require(1U);
        return std::to_integer<std::uint8_t>(bytes_[cursor_++]);
    }

    template <typename UInt> [[nodiscard]] UInt integer() {
        static_assert(std::is_unsigned_v<UInt>);
        require(sizeof(UInt));
        const auto value = read_le<UInt>(bytes_, cursor_);
        cursor_ += sizeof(UInt);
        return value;
    }

    [[nodiscard]] std::string text() {
        const auto length = integer<std::uint32_t>();
        require(length);
        const auto *begin = reinterpret_cast<const char *>(bytes_.data() + cursor_);
        cursor_ += length;
        return {begin, length};
    }

    [[nodiscard]] bool finished() const noexcept { return cursor_ == bytes_.size(); }

  private:
    void require(const std::size_t count) const {
        if (count > bytes_.size() - cursor_)
            throw FactBatchError::malformed_fact;
    }

    std::span<const std::byte> bytes_;
    std::size_t cursor_{};
};

void write_availability(BinaryFactWriter &writer, const Availability availability) {
    const auto value = static_cast<std::uint8_t>(availability);
    if (value >= availability_names.size())
        throw FactBatchError::invalid_fact;
    writer.byte(value);
}

Availability read_availability(BinaryFactReader &reader) {
    const auto value = reader.byte();
    if (value >= availability_names.size())
        throw FactBatchError::malformed_fact;
    return static_cast<Availability>(value);
}

void write_fact_id(BinaryFactWriter &writer, const FactId &id) {
    writer.text(id.run_id);
    writer.text(id.producer_id);
    writer.integer(id.producer_sequence);
}

FactId read_fact_id(BinaryFactReader &reader) {
    return {reader.text(), reader.text(), reader.integer<std::uint64_t>()};
}

void write_string_field(BinaryFactWriter &writer, const Field<std::string> &field) {
    write_availability(writer, field.availability);
    writer.text(field.reason_code);
    if (field.value)
        writer.text(*field.value);
}

Field<std::string> read_string_field(BinaryFactReader &reader) {
    Field<std::string> field;
    field.availability = read_availability(reader);
    field.reason_code = reader.text();
    if (field.availability == Availability::known)
        field.value = reader.text();
    return field;
}

void write_binary_field(BinaryFactWriter &writer, const FactField &field) {
    writer.text(field.name);
    write_availability(writer, field.availability);
    const auto unit = static_cast<std::uint8_t>(field.unit);
    if (unit >= unit_names.size())
        throw FactBatchError::invalid_fact;
    writer.byte(unit);
    writer.byte(static_cast<std::uint8_t>(field.value.index()));
    writer.text(field.unavailable_reason);
    if (const auto *boolean = std::get_if<bool>(&field.value)) {
        writer.byte(*boolean ? 1U : 0U);
    } else if (const auto *unsigned_integer = std::get_if<std::uint64_t>(&field.value)) {
        writer.integer(*unsigned_integer);
    } else if (const auto *signed_integer = std::get_if<std::int64_t>(&field.value)) {
        writer.integer(std::bit_cast<std::uint64_t>(*signed_integer));
    } else if (const auto *number = std::get_if<double>(&field.value)) {
        writer.integer(std::bit_cast<std::uint64_t>(*number));
    } else if (const auto *text = std::get_if<std::string>(&field.value)) {
        writer.text(*text);
    } else if (const auto *id = std::get_if<FactId>(&field.value)) {
        write_fact_id(writer, *id);
    }
}

FactField read_binary_field(BinaryFactReader &reader) {
    FactField field;
    field.name = reader.text();
    field.availability = read_availability(reader);
    const auto unit = reader.byte();
    if (unit >= unit_names.size())
        throw FactBatchError::malformed_fact;
    field.unit = static_cast<FactFieldUnit>(unit);
    const auto type = reader.byte();
    field.unavailable_reason = reader.text();
    switch (type) {
    case 0U:
        field.value = std::monostate{};
        break;
    case 1U: {
        const auto value = reader.byte();
        if (value > 1U)
            throw FactBatchError::malformed_fact;
        field.value = value != 0U;
        break;
    }
    case 2U:
        field.value = reader.integer<std::uint64_t>();
        break;
    case 3U:
        field.value = std::bit_cast<std::int64_t>(reader.integer<std::uint64_t>());
        break;
    case 4U:
        field.value = std::bit_cast<double>(reader.integer<std::uint64_t>());
        break;
    case 5U:
        field.value = reader.text();
        break;
    case 6U:
        field.value = read_fact_id(reader);
        break;
    default:
        throw FactBatchError::malformed_fact;
    }
    return field;
}

std::vector<std::byte> encode_binary_fact(const Fact &fact) {
    if (!well_formed(fact))
        throw FactBatchError::invalid_fact;
    BinaryFactWriter writer;
    for (const auto byte : binary_fact_magic)
        writer.byte(std::to_integer<std::uint8_t>(byte));
    writer.byte(binary_fact_schema_major);
    writer.byte(binary_fact_schema_minor);
    writer.integer(std::uint16_t{});
    write_fact_id(writer, fact.id);
    writer.text(fact.kind);
    write_availability(writer, fact.frame_index.availability);
    writer.text(fact.frame_index.reason_code);
    if (fact.frame_index.value)
        writer.integer(*fact.frame_index.value);
    writer.integer(static_cast<std::uint32_t>(fact.cause_ids.size()));
    for (const auto &cause : fact.cause_ids) {
        if (const auto *id = std::get_if<FactId>(&cause)) {
            writer.byte(0U);
            write_fact_id(writer, *id);
        } else {
            writer.byte(1U);
            writer.text(std::get<PreexistingContext>(cause).context_id);
        }
    }
    write_string_field(writer, fact.decision_id);
    write_string_field(writer, fact.assignment_id);
    write_string_field(writer, fact.occurrence_id);
    write_string_field(writer, fact.reason_code);
    write_availability(writer, fact.shared_state_order.availability);
    writer.text(fact.shared_state_order.reason_code);
    if (fact.shared_state_order.value) {
        writer.integer(static_cast<std::uint32_t>(fact.shared_state_order.value->size()));
        for (const auto &order : *fact.shared_state_order.value) {
            writer.text(order.state_id);
            writer.integer(order.sequence);
        }
    }
    writer.integer(static_cast<std::uint32_t>(fact.fields.size()));
    for (const auto &field : fact.fields)
        write_binary_field(writer, field);
    return std::move(writer).finish();
}

Fact decode_binary_fact(const std::span<const std::byte> bytes) {
    BinaryFactReader reader{bytes};
    for (const auto expected : binary_fact_magic)
        if (reader.byte() != std::to_integer<std::uint8_t>(expected))
            throw FactBatchError::malformed_fact;
    if (reader.byte() != binary_fact_schema_major || reader.byte() != binary_fact_schema_minor ||
        reader.integer<std::uint16_t>() != 0U)
        throw FactBatchError::malformed_fact;
    Fact fact;
    fact.id = read_fact_id(reader);
    fact.kind = reader.text();
    fact.frame_index.availability = read_availability(reader);
    fact.frame_index.reason_code = reader.text();
    if (fact.frame_index.availability == Availability::known)
        fact.frame_index.value = reader.integer<std::uint64_t>();
    const auto cause_count = reader.integer<std::uint32_t>();
    if (cause_count > max_fact_causes)
        throw FactBatchError::malformed_fact;
    fact.cause_ids.reserve(cause_count);
    for (std::uint32_t index{}; index < cause_count; ++index) {
        const auto type = reader.byte();
        if (type == 0U)
            fact.cause_ids.emplace_back(read_fact_id(reader));
        else if (type == 1U)
            fact.cause_ids.emplace_back(PreexistingContext{reader.text()});
        else
            throw FactBatchError::malformed_fact;
    }
    fact.decision_id = read_string_field(reader);
    fact.assignment_id = read_string_field(reader);
    fact.occurrence_id = read_string_field(reader);
    fact.reason_code = read_string_field(reader);
    fact.shared_state_order.availability = read_availability(reader);
    fact.shared_state_order.reason_code = reader.text();
    if (fact.shared_state_order.availability == Availability::known) {
        const auto order_count = reader.integer<std::uint32_t>();
        if (order_count > max_fact_state_orders)
            throw FactBatchError::malformed_fact;
        fact.shared_state_order.value.emplace();
        fact.shared_state_order.value->reserve(order_count);
        for (std::uint32_t index{}; index < order_count; ++index)
            fact.shared_state_order.value->push_back(
                {reader.text(), reader.integer<std::uint64_t>()});
    }
    const auto field_count = reader.integer<std::uint32_t>();
    if (field_count > max_fact_fields)
        throw FactBatchError::malformed_fact;
    fact.fields.reserve(field_count);
    for (std::uint32_t index{}; index < field_count; ++index)
        fact.fields.push_back(read_binary_field(reader));
    if (!reader.finished() || !well_formed(fact))
        throw FactBatchError::malformed_fact;
    return fact;
}

bool is_binary_fact(const std::span<const std::byte> bytes) noexcept {
    return bytes.size() >= binary_fact_magic.size() &&
           std::equal(binary_fact_magic.begin(), binary_fact_magic.end(), bytes.begin());
}

toml::table string_field(const Field<std::string> &field) {
    toml::table table{{"availability", availability_name(field.availability)},
                      {"reason_code", field.reason_code}};
    if (field.value) {
        table.insert("value", *field.value);
    }
    return table;
}

Field<std::string> parse_string_field(const toml::node_view<const toml::node> node) {
    const auto *table = node.as_table();
    if (table == nullptr) {
        throw FactBatchError::malformed_fact;
    }
    Field<std::string> field;
    field.availability = availability_value(*table);
    field.reason_code = table->get_as<std::string>("reason_code")
                            ? table->get_as<std::string>("reason_code")->get()
                            : std::string{};
    if (const auto value = (*table)["value"].value<std::string>()) {
        field.value = *value;
    }
    return field;
}

const char *unit_name(const FactFieldUnit unit) {
    const auto index = static_cast<std::size_t>(unit);
    if (index >= unit_names.size())
        throw FactBatchError::invalid_fact;
    return unit_names[index];
}

FactFieldUnit unit_value(const toml::table &table) {
    const auto text = table["unit"].value<std::string>();
    if (!text)
        throw FactBatchError::malformed_fact;
    for (std::size_t index{}; index < unit_names.size(); ++index)
        if (*text == unit_names[index])
            return static_cast<FactFieldUnit>(index);
    throw FactBatchError::malformed_fact;
}

std::string double_text(const double value) {
    std::array<char, 64> buffer{};
    const auto converted =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                      std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (converted.ec != std::errc{})
        throw FactBatchError::invalid_fact;
    return {buffer.data(), converted.ptr};
}

toml::array compact_observation_field(const FactField &field) {
    std::string type{"none"};
    std::string value;
    std::string fact_run;
    std::string fact_producer;
    std::string fact_sequence;
    if (const auto *boolean = std::get_if<bool>(&field.value)) {
        type = "bool";
        value = *boolean ? "true" : "false";
    } else if (const auto *unsigned_integer = std::get_if<std::uint64_t>(&field.value)) {
        type = "uint64";
        value = std::to_string(*unsigned_integer);
    } else if (const auto *signed_integer = std::get_if<std::int64_t>(&field.value)) {
        type = "int64";
        value = std::to_string(*signed_integer);
    } else if (const auto *number = std::get_if<double>(&field.value)) {
        type = "double";
        value = double_text(*number);
    } else if (const auto *text = std::get_if<std::string>(&field.value)) {
        type = "string";
        value = *text;
    } else if (const auto *id = std::get_if<FactId>(&field.value)) {
        type = "fact_id";
        fact_run = id->run_id;
        fact_producer = id->producer_id;
        fact_sequence = std::to_string(id->producer_sequence);
    }
    return toml::array{field.name,
                       availability_name(field.availability),
                       unit_name(field.unit),
                       type,
                       value,
                       field.unavailable_reason,
                       fact_run,
                       fact_producer,
                       fact_sequence};
}

std::int64_t signed_decimal(const toml::node_view<const toml::node> node) {
    const auto text = node.value<std::string>();
    if (!text || text->empty() || text->front() == '+')
        throw FactBatchError::malformed_fact;
    std::int64_t value{};
    const auto parsed = std::from_chars(text->data(), text->data() + text->size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size())
        throw FactBatchError::malformed_fact;
    return value;
}

std::int64_t signed_decimal_text(const std::string_view text) {
    if (text.empty() || text.front() == '+')
        throw FactBatchError::malformed_fact;
    std::int64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw FactBatchError::malformed_fact;
    return value;
}

FactField parse_observation_field(const toml::node &node) {
    const auto *table = node.as_table();
    const auto name = table ? (*table)["name"].value<std::string>() : std::nullopt;
    const auto reason = table ? (*table)["unavailable_reason"].value<std::string>() : std::nullopt;
    const auto type = table ? (*table)["value_type"].value<std::string>() : std::nullopt;
    if (!table || !name || !reason || !type)
        throw FactBatchError::malformed_fact;
    FactField field;
    field.name = *name;
    field.availability = availability_value(*table);
    field.unit = unit_value(*table);
    field.unavailable_reason = *reason;
    if (*type == "none") {
        field.value = std::monostate{};
    } else if (*type == "bool") {
        const auto value = (*table)["value"].value<bool>();
        if (!value)
            throw FactBatchError::malformed_fact;
        field.value = *value;
    } else if (*type == "uint64") {
        field.value = decimal((*table)["value"]);
    } else if (*type == "int64") {
        field.value = signed_decimal((*table)["value"]);
    } else if (*type == "double") {
        const auto value = (*table)["value"].value<double>();
        if (!value)
            throw FactBatchError::malformed_fact;
        field.value = *value;
    } else if (*type == "string") {
        const auto value = (*table)["value"].value<std::string>();
        if (!value)
            throw FactBatchError::malformed_fact;
        field.value = *value;
    } else if (*type == "fact_id") {
        const auto *value = (*table)["value"].as_table();
        const auto run = value ? (*value)["run_id"].value<std::string>() : std::nullopt;
        const auto producer = value ? (*value)["producer_id"].value<std::string>() : std::nullopt;
        if (!value || !run || !producer)
            throw FactBatchError::malformed_fact;
        field.value = FactId{*run, *producer, decimal((*value)["producer_sequence"])};
    } else {
        throw FactBatchError::malformed_fact;
    }
    return field;
}

FactField parse_compact_observation_field(const toml::node &node) {
    const auto *entry = node.as_array();
    if (!entry || entry->size() != 9U)
        throw FactBatchError::malformed_fact;
    std::array<std::string, 9> values;
    for (std::size_t index{}; index < values.size(); ++index) {
        const auto value = entry->get(index)->value<std::string>();
        if (!value)
            throw FactBatchError::malformed_fact;
        values[index] = *value;
    }
    toml::table descriptors{{"availability", values[1]}, {"unit", values[2]}};
    FactField field;
    field.name = std::move(values[0]);
    field.availability = availability_value(descriptors);
    field.unit = unit_value(descriptors);
    field.unavailable_reason = std::move(values[5]);
    if (values[3] == "none") {
        field.value = std::monostate{};
    } else if (values[3] == "bool") {
        if (values[4] != "true" && values[4] != "false")
            throw FactBatchError::malformed_fact;
        field.value = values[4] == "true";
    } else if (values[3] == "uint64") {
        field.value = decimal_text(values[4]);
    } else if (values[3] == "int64") {
        field.value = signed_decimal_text(values[4]);
    } else if (values[3] == "double") {
        double parsed{};
        const auto converted =
            std::from_chars(values[4].data(), values[4].data() + values[4].size(), parsed);
        if (converted.ec != std::errc{} || converted.ptr != values[4].data() + values[4].size())
            throw FactBatchError::malformed_fact;
        field.value = parsed;
    } else if (values[3] == "string") {
        field.value = std::move(values[4]);
    } else if (values[3] == "fact_id") {
        field.value = FactId{std::move(values[6]), std::move(values[7]), decimal_text(values[8])};
    } else {
        throw FactBatchError::malformed_fact;
    }
    return field;
}

template <class T> bool compact_default(const Field<T> &field) {
    return field.availability == Availability::not_applicable && !field.value &&
           field.reason_code == compact_default_reason;
}

bool compactable(const Fact &fact) {
    return fact.fields.empty() && fact.cause_ids.empty() && compact_default(fact.decision_id) &&
           compact_default(fact.assignment_id) && compact_default(fact.occurrence_id) &&
           compact_default(fact.reason_code) && compact_default(fact.shared_state_order);
}

std::string encode_fact(const Fact &fact) {
    if (!well_formed(fact)) {
        throw FactBatchError::invalid_fact;
    }
    toml::table frame{{"availability", availability_name(fact.frame_index.availability)},
                      {"reason_code", fact.frame_index.reason_code}};
    if (fact.frame_index.value) {
        frame.insert("value", std::to_string(*fact.frame_index.value));
    }
    if (compactable(fact)) {
        toml::table table{{"schema_version", 1},
                          {"schema_minor", 1},
                          {"kind", fact.kind},
                          {"run_id", fact.id.run_id},
                          {"producer_id", fact.id.producer_id},
                          {"producer_sequence", std::to_string(fact.id.producer_sequence)},
                          {"frame_index", std::move(frame)}};
        std::ostringstream stream;
        stream << table;
        return stream.str();
    }
    toml::array causes;
    for (const auto &cause : fact.cause_ids) {
        if (const auto *id = std::get_if<FactId>(&cause)) {
            causes.push_back(
                toml::table{{"type", "fact"},
                            {"run_id", id->run_id},
                            {"producer_id", id->producer_id},
                            {"producer_sequence", std::to_string(id->producer_sequence)}});
        } else {
            causes.push_back(
                toml::table{{"type", "preexisting"},
                            {"context_id", std::get<PreexistingContext>(cause).context_id}});
        }
    }
    toml::table order{{"availability", availability_name(fact.shared_state_order.availability)},
                      {"reason_code", fact.shared_state_order.reason_code}};
    if (fact.shared_state_order.value) {
        toml::array values;
        for (const auto &item : *fact.shared_state_order.value) {
            values.push_back(toml::table{{"state_id", item.state_id},
                                         {"sequence", std::to_string(item.sequence)}});
        }
        order.insert("value", std::move(values));
    }
    toml::table table{{"schema_version", 1},
                      {"schema_minor", fact.fields.empty() ? 0 : 3},
                      {"kind", fact.kind},
                      {"run_id", fact.id.run_id},
                      {"producer_id", fact.id.producer_id},
                      {"producer_sequence", std::to_string(fact.id.producer_sequence)},
                      {"frame_index", std::move(frame)},
                      {"causes", std::move(causes)},
                      {"decision_id", string_field(fact.decision_id)},
                      {"assignment_id", string_field(fact.assignment_id)},
                      {"occurrence_id", string_field(fact.occurrence_id)},
                      {"reason", string_field(fact.reason_code)},
                      {"shared_state_order", std::move(order)}};
    if (!fact.fields.empty()) {
        toml::array fields;
        for (const auto &field : fact.fields)
            fields.push_back(compact_observation_field(field));
        table.insert("fields", std::move(fields));
    }
    std::ostringstream stream;
    stream << table;
    return stream.str();
}

Fact decode_fact(const std::string_view text) {
    try {
        const toml::table table = toml::parse(text);
        const auto schema_minor = table["schema_minor"].value<std::int64_t>();
        if (table["schema_version"].value<std::int64_t>() != 1 || !schema_minor ||
            (*schema_minor != 0 && *schema_minor != 1 && *schema_minor != 2 &&
             *schema_minor != 3)) {
            throw FactBatchError::malformed_fact;
        }
        const bool compact = *schema_minor == 1;
        const bool detailed = *schema_minor == 2 || *schema_minor == 3;
        const bool compact_details = *schema_minor == 3;
        Fact fact;
        const auto required = [&](const std::string_view key) {
            const auto value = table[key].value<std::string>();
            if (!value) {
                throw FactBatchError::malformed_fact;
            }
            return *value;
        };
        fact.kind = required("kind");
        fact.id = {required("run_id"), required("producer_id"),
                   decimal(table["producer_sequence"])};
        const auto *frame = table["frame_index"].as_table();
        if (frame == nullptr) {
            throw FactBatchError::malformed_fact;
        }
        fact.frame_index.availability = availability_value(*frame);
        fact.frame_index.reason_code = frame->get_as<std::string>("reason_code")
                                           ? frame->get_as<std::string>("reason_code")->get()
                                           : std::string{};
        if (frame->contains("value")) {
            fact.frame_index.value = decimal((*frame)["value"]);
        }
        const auto *causes = table["causes"].as_array();
        if (causes == nullptr && !compact) {
            throw FactBatchError::malformed_fact;
        }
        if (causes != nullptr) {
            for (const auto &node : *causes) {
                const auto *cause = node.as_table();
                if (cause == nullptr) {
                    throw FactBatchError::malformed_fact;
                }
                const auto type = (*cause)["type"].value<std::string>();
                if (type == "fact") {
                    const auto run = (*cause)["run_id"].value<std::string>();
                    const auto producer = (*cause)["producer_id"].value<std::string>();
                    if (!run || !producer) {
                        throw FactBatchError::malformed_fact;
                    }
                    fact.cause_ids.emplace_back(
                        FactId{*run, *producer, decimal((*cause)["producer_sequence"])});
                } else if (type == "preexisting") {
                    const auto context = (*cause)["context_id"].value<std::string>();
                    if (!context) {
                        throw FactBatchError::malformed_fact;
                    }
                    fact.cause_ids.emplace_back(PreexistingContext{*context});
                } else {
                    throw FactBatchError::malformed_fact;
                }
            }
        }
        const auto parse_optional = [&](const std::string_view key, Field<std::string> &field) {
            if (table.contains(key)) {
                field = parse_string_field(table[key]);
            } else if (compact) {
                field = {Availability::not_applicable, std::nullopt,
                         std::string{compact_default_reason}};
            } else {
                throw FactBatchError::malformed_fact;
            }
        };
        parse_optional("decision_id", fact.decision_id);
        parse_optional("assignment_id", fact.assignment_id);
        parse_optional("occurrence_id", fact.occurrence_id);
        parse_optional("reason", fact.reason_code);
        const auto *order = table["shared_state_order"].as_table();
        if (order == nullptr && !compact) {
            throw FactBatchError::malformed_fact;
        }
        if (order == nullptr) {
            fact.shared_state_order = {Availability::not_applicable, std::nullopt,
                                       std::string{compact_default_reason}};
        } else {
            fact.shared_state_order.availability = availability_value(*order);
            fact.shared_state_order.reason_code =
                order->get_as<std::string>("reason_code")
                    ? order->get_as<std::string>("reason_code")->get()
                    : std::string{};
            if (const auto *values = (*order)["value"].as_array()) {
                fact.shared_state_order.value.emplace();
                for (const auto &node : *values) {
                    const auto *item = node.as_table();
                    const auto state =
                        item ? (*item)["state_id"].value<std::string>() : std::nullopt;
                    if (!item || !state) {
                        throw FactBatchError::malformed_fact;
                    }
                    fact.shared_state_order.value->push_back(
                        {*state, decimal((*item)["sequence"])});
                }
            }
        }
        const auto *fields = table["fields"].as_array();
        if ((detailed && fields == nullptr) || (!detailed && fields != nullptr) ||
            (fields != nullptr && fields->size() > max_fact_fields)) {
            throw FactBatchError::malformed_fact;
        }
        if (fields != nullptr) {
            fact.fields.reserve(fields->size());
            for (const auto &node : *fields)
                fact.fields.push_back(compact_details ? parse_compact_observation_field(node)
                                                      : parse_observation_field(node));
        }
        if (!well_formed(fact)) {
            throw FactBatchError::malformed_fact;
        }
        return fact;
    } catch (const toml::parse_error &) {
        throw FactBatchError::malformed_fact;
    }
}

} // namespace

EncodedFactBatch encode_fact_batch(const std::vector<Fact> &facts, const std::uint64_t sequence) {
    try {
        if (facts.empty() || facts.size() > max_fact_batch_records || sequence == 0) {
            return FactBatchError::invalid_count;
        }
        std::vector<std::byte> payload;
        append_le(payload, static_cast<std::uint32_t>(facts.size()));
        for (const auto &fact : facts) {
            const auto record = fact.fields.empty()
                                    ? [&] {
                                          const auto text = encode_fact(fact);
                                          return std::vector<std::byte>{
                                              reinterpret_cast<const std::byte *>(text.data()),
                                              reinterpret_cast<const std::byte *>(text.data() +
                                                                                  text.size())};
                                      }()
                                    : encode_binary_fact(fact);
            if (record.empty() || record.size() + sizeof(std::uint32_t) > max_encoded_fact_bytes) {
                return FactBatchError::fact_too_large;
            }
            if (payload.size() >
                max_protocol_payload_bytes - sizeof(std::uint32_t) - record.size()) {
                return FactBatchError::batch_too_large;
            }
            append_le(payload, static_cast<std::uint32_t>(record.size()));
            payload.insert(payload.end(), record.begin(), record.end());
        }
        const auto header = encode_protocol_header(
            {MessageType::fact_batch, static_cast<std::uint32_t>(payload.size()), 2, sequence});
        std::vector<std::byte> message(header.begin(), header.end());
        message.insert(message.end(), payload.begin(), payload.end());
        return message;
    } catch (const FactBatchError error) {
        return error;
    }
}

DecodedFactBatch decode_fact_batch(const std::span<const std::byte> message,
                                   const std::uint64_t expected_sequence) {
    if (message.size() < protocol_header_bytes) {
        return FactBatchError::header_rejected;
    }
    const auto header = decode_protocol_header(message.first(protocol_header_bytes), 2);
    if (header.error != HeaderError::none) {
        return FactBatchError::header_rejected;
    }
    if (header.header.type != MessageType::fact_batch) {
        return FactBatchError::wrong_message_type;
    }
    if (header.header.channel_sequence != expected_sequence) {
        return FactBatchError::sequence_mismatch;
    }
    if (header.header.payload_bytes != message.size() - protocol_header_bytes) {
        return FactBatchError::length_mismatch;
    }
    auto payload = message.subspan(protocol_header_bytes);
    if (payload.size() < sizeof(std::uint32_t)) {
        return FactBatchError::truncated;
    }
    const auto count = read_le<std::uint32_t>(payload, 0);
    if (count == 0 || count > max_fact_batch_records) {
        return FactBatchError::invalid_count;
    }
    std::size_t cursor = sizeof(std::uint32_t);
    std::vector<Fact> facts;
    facts.reserve(count);
    try {
        for (std::uint32_t index = 0; index < count; ++index) {
            if (payload.size() - cursor < sizeof(std::uint32_t)) {
                return FactBatchError::truncated;
            }
            const auto length = read_le<std::uint32_t>(payload, cursor);
            cursor += sizeof(std::uint32_t);
            if (length == 0 || length + sizeof(std::uint32_t) > max_encoded_fact_bytes) {
                return FactBatchError::fact_too_large;
            }
            if (length > payload.size() - cursor) {
                return FactBatchError::truncated;
            }
            const auto record = payload.subspan(cursor, length);
            facts.push_back(is_binary_fact(record)
                                ? decode_binary_fact(record)
                                : decode_fact(std::string_view{
                                      reinterpret_cast<const char *>(record.data()), record.size()}));
            cursor += length;
        }
    } catch (const FactBatchError error) {
        return error;
    }
    if (cursor != payload.size()) {
        return FactBatchError::length_mismatch;
    }
    return facts;
}

} // namespace ayther::audio_qa
