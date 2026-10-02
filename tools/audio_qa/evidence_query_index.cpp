#include "evidence_query_index.h"

#include "model_limits.h"

#include <charconv>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string_view>
#include <toml++/toml.hpp>
#include <utility>

namespace ayther::audio_qa {
namespace {

constexpr std::size_t max_query_entries = 4096;

bool identity(std::string_view value) noexcept {
    if (value.empty() || value.size() > max_identity_bytes)
        return false;
    for (const unsigned char character : value)
        if (character < 0x20 || character == 0x7f)
            return false;
    return true;
}

bool valid(const FactId &id) noexcept {
    return identity(id.run_id) && identity(id.producer_id) && id.producer_sequence != 0;
}

bool valid(const EvidenceQueryEntry &entry) noexcept {
    if (!valid(entry.origin) || !identity(entry.route.mix_span_id) ||
        !identity(entry.route.relation_reason) || !well_formed(entry.route.output_range) ||
        entry.route.fact_path.empty() || entry.route.fact_path.size() > max_fact_causes ||
        entry.route.fact_path.front() != entry.origin)
        return false;
    for (const auto &id : entry.route.fact_path)
        if (!valid(id) || id.run_id != entry.origin.run_id)
            return false;
    return entry.route.basis == TraceRelationBasis::explicit_cause ||
           entry.route.basis == TraceRelationBasis::temporal_correlation;
}

std::optional<std::uint64_t> unsigned_value(std::string_view value) noexcept {
    if (value.empty() || value.front() == '+' || value.front() == '-')
        return std::nullopt;
    std::uint64_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        return std::nullopt;
    return result;
}

std::string required_text(const toml::table &table, std::string_view key) {
    const auto value = table[key].value<std::string>();
    if (!value || !identity(*value))
        throw EvidenceQueryIndexError::malformed_document;
    return *value;
}

FactId fact_id(const toml::table &table) {
    FactId result;
    result.run_id = required_text(table, "run_id");
    result.producer_id = required_text(table, "producer_id");
    const auto sequence = table["sequence"].value<std::string>();
    if (!sequence)
        throw EvidenceQueryIndexError::malformed_document;
    const auto parsed = unsigned_value(*sequence);
    if (!parsed || *parsed == 0)
        throw EvidenceQueryIndexError::malformed_document;
    result.producer_sequence = *parsed;
    return result;
}

toml::table fact_id_table(const FactId &id) {
    return toml::table{{"run_id", id.run_id},
                       {"producer_id", id.producer_id},
                       {"sequence", std::to_string(id.producer_sequence)}};
}

std::string basis_name(TraceRelationBasis basis) {
    return basis == TraceRelationBasis::explicit_cause ? "explicit_cause" : "temporal_correlation";
}

TraceRelationBasis parse_basis(const toml::table &table) {
    const auto value = table["basis"].value<std::string>();
    if (value == "explicit_cause")
        return TraceRelationBasis::explicit_cause;
    if (value == "temporal_correlation")
        return TraceRelationBasis::temporal_correlation;
    throw EvidenceQueryIndexError::malformed_document;
}

} // namespace

EvidenceQueryIndexResult make_evidence_query_index(const FactId &origin,
                                                   const EventAudioQueryResult &result) {
    if (!valid(origin) || result.status != EventAudioQueryStatus::found || result.routes.empty() ||
        result.routes.size() > max_query_entries)
        return EvidenceQueryIndexError::invalid_content;
    EvidenceQueryIndex index;
    index.entries.reserve(result.routes.size());
    for (const auto &route : result.routes) {
        EvidenceQueryEntry entry{origin, route};
        if (!valid(entry))
            return EvidenceQueryIndexError::invalid_content;
        index.entries.push_back(std::move(entry));
    }
    return index;
}

EvidenceQueryIndexEncodeResult evidence_query_index_to_toml(const EvidenceQueryIndex &index) {
    try {
        if (index.entries.empty() || index.entries.size() > max_query_entries)
            return EvidenceQueryIndexError::invalid_content;
        toml::array entries;
        for (const auto &entry : index.entries) {
            if (!valid(entry))
                return EvidenceQueryIndexError::invalid_content;
            toml::array path;
            for (const auto &id : entry.route.fact_path)
                path.push_back(fact_id_table(id));
            entries.push_back(toml::table{
                {"origin", fact_id_table(entry.origin)},
                {"basis", basis_name(entry.route.basis)},
                {"fact_path", std::move(path)},
                {"mix_span_id", entry.route.mix_span_id},
                {"output_timeline", entry.route.output_range.timeline_id},
                {"output_sample_rate",
                 static_cast<std::int64_t>(entry.route.output_range.sample_rate)},
                {"output_begin", std::to_string(entry.route.output_range.begin)},
                {"output_end", std::to_string(entry.route.output_range.end)},
                {"relation_reason", entry.route.relation_reason},
            });
        }
        std::ostringstream output;
        output << toml::table{{"schema_version", 1},
                              {"schema_minor", 0},
                              {"kind", "event_audio_query_index"},
                              {"entries", std::move(entries)}};
        auto text = output.str();
        if (text.size() > max_metadata_bytes)
            return EvidenceQueryIndexError::too_large;
        return text;
    } catch (...) {
        return EvidenceQueryIndexError::invalid_content;
    }
}

EvidenceQueryIndexResult evidence_query_index_from_toml(std::string_view text) {
    if (text.size() > max_metadata_bytes)
        return EvidenceQueryIndexError::too_large;
    try {
        const auto document = toml::parse(text);
        const auto major = document["schema_version"].value<std::int64_t>();
        const auto minor = document["schema_minor"].value<std::int64_t>();
        const auto kind = document["kind"].value<std::string>();
        if (!major || !minor || *major != 1 || *minor != 0)
            return EvidenceQueryIndexError::incompatible_version;
        if (!kind || *kind != "event_audio_query_index")
            return EvidenceQueryIndexError::malformed_document;
        const auto *entries = document["entries"].as_array();
        if (!entries || entries->empty() || entries->size() > max_query_entries)
            return EvidenceQueryIndexError::malformed_document;
        EvidenceQueryIndex result;
        result.entries.reserve(entries->size());
        for (const auto &node : *entries) {
            const auto *table = node.as_table();
            const auto *origin_table = table ? (*table)["origin"].as_table() : nullptr;
            const auto *path = table ? (*table)["fact_path"].as_array() : nullptr;
            const auto rate =
                table ? (*table)["output_sample_rate"].value<std::int64_t>() : std::nullopt;
            const auto begin_text =
                table ? (*table)["output_begin"].value<std::string>() : std::nullopt;
            const auto end_text =
                table ? (*table)["output_end"].value<std::string>() : std::nullopt;
            const auto begin = begin_text ? unsigned_value(*begin_text) : std::nullopt;
            const auto end = end_text ? unsigned_value(*end_text) : std::nullopt;
            if (!table || !origin_table || !path || path->empty() ||
                path->size() > max_fact_causes || !rate || *rate <= 0 || *rate > 192000 || !begin ||
                !end)
                return EvidenceQueryIndexError::malformed_document;
            EvidenceQueryEntry entry;
            entry.origin = fact_id(*origin_table);
            entry.route.basis = parse_basis(*table);
            entry.route.mix_span_id = required_text(*table, "mix_span_id");
            entry.route.output_range = {required_text(*table, "output_timeline"),
                                        static_cast<std::uint32_t>(*rate), *begin, *end};
            entry.route.relation_reason = required_text(*table, "relation_reason");
            for (const auto &path_node : *path) {
                const auto *path_table = path_node.as_table();
                if (!path_table)
                    return EvidenceQueryIndexError::malformed_document;
                entry.route.fact_path.push_back(fact_id(*path_table));
            }
            if (!valid(entry))
                return EvidenceQueryIndexError::invalid_content;
            result.entries.push_back(std::move(entry));
        }
        return result;
    } catch (const toml::parse_error &) {
        return EvidenceQueryIndexError::malformed_document;
    } catch (const EvidenceQueryIndexError error) {
        return error;
    } catch (...) {
        return EvidenceQueryIndexError::malformed_document;
    }
}

EvidenceQueryIndexResult read_evidence_query_index(const std::filesystem::path &path) noexcept {
    try {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            return EvidenceQueryIndexError::io_error;
        if (size > max_metadata_bytes)
            return EvidenceQueryIndexError::too_large;
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return EvidenceQueryIndexError::io_error;
        const std::string text{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
        if (input.bad() || text.size() != size)
            return EvidenceQueryIndexError::io_error;
        return evidence_query_index_from_toml(text);
    } catch (...) {
        return EvidenceQueryIndexError::io_error;
    }
}

std::vector<EventAudioRoute> query_evidence_index(const EvidenceQueryIndex &index,
                                                  const FactId &origin) {
    std::vector<EventAudioRoute> routes;
    for (const auto &entry : index.entries)
        if (entry.origin == origin)
            routes.push_back(entry.route);
    return routes;
}

std::string format_evidence_query(const FactId &origin, std::span<const EventAudioRoute> routes) {
    std::ostringstream output;
    output << "audio_qa_query: event=" << origin.run_id << '/' << origin.producer_id << '/'
           << origin.producer_sequence << " status=" << (routes.empty() ? "not_found" : "found")
           << " route_count=" << routes.size();
    for (const auto &route : routes) {
        output << "\nroute basis=" << basis_name(route.basis) << " mix_span=" << route.mix_span_id
               << " audio=" << route.output_range.timeline_id << '@'
               << route.output_range.sample_rate << "[" << route.output_range.begin << ','
               << route.output_range.end << ") reason=" << route.relation_reason << " path=";
        for (std::size_t index{}; index < route.fact_path.size(); ++index) {
            if (index != 0)
                output << "->";
            const auto &id = route.fact_path[index];
            output << id.run_id << '/' << id.producer_id << '/' << id.producer_sequence;
        }
    }
    return output.str();
}

} // namespace ayther::audio_qa
