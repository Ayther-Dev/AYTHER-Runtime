#include "request_summary_store.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <system_error>
#include <utility>

namespace ayther::audio_qa {
namespace {

constexpr std::uintmax_t max_request_summary_bytes = 16U * 1024U * 1024U;

std::string hexadecimal(const std::array<std::uint8_t, 32> &bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text(bytes.size() * 2U, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        text[index * 2U] = digits[bytes[index] >> 4U];
        text[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
    }
    return text;
}

toml::array string_array(const std::vector<std::string> &values) {
    toml::array array;
    for (const auto &value : values)
        array.push_back(value);
    return array;
}

std::optional<CheckTechnicalOutcome> parse_outcome(std::string_view code) noexcept {
    for (const auto outcome :
         {CheckTechnicalOutcome::complete, CheckTechnicalOutcome::incomplete,
          CheckTechnicalOutcome::invalid_request, CheckTechnicalOutcome::preservation_failure})
        if (check_technical_outcome_code(outcome) == code)
            return outcome;
    return std::nullopt;
}

std::optional<ValueSource> parse_source(std::string_view code) noexcept {
    for (const auto source :
         {ValueSource::explicit_option, ValueSource::play_manifest, ValueSource::reference,
          ValueSource::environment, ValueSource::default_value, ValueSource::generated})
        if (value_source_code(source) == code)
            return source;
    return std::nullopt;
}

bool read_strings(const toml::node_view<const toml::node> node, std::vector<std::string> &out) {
    const auto *array = node.as_array();
    if (array == nullptr)
        return false;
    for (const auto &element : *array) {
        const auto value = element.value<std::string>();
        if (!value)
            return false;
        out.push_back(*value);
    }
    return true;
}

} // namespace

std::filesystem::path request_summary_path(const std::filesystem::path &output,
                                           std::string_view run_id) {
    return output / "requests" / std::string{run_id} / "request-summary.toml";
}

StoredMaterial stored_material(const MaterialPin &pin) {
    return {std::string{material_pin_role_code(pin.role)}, pin.field, pin.path.string(),
            hexadecimal(pin.content.sha256), pin.content.byte_size};
}

std::string format_request_summary(const RequestSummaryDocument &summary) {
    toml::table document{{"schema_version", request_summary_schema_version},
                         {"schema_minor", request_summary_schema_minor},
                         {"kind", "request_summary"},
                         {"request_id", summary.request_id},
                         {"conditions_id", summary.conditions_id},
                         {"run_id", summary.run_id},
                         {"confirmed", summary.confirmed},
                         {"exit_code", static_cast<std::int64_t>(summary.exit_code)},
                         {"linear_complete", summary.linear_complete},
                         {"visual_evaluation", "not_evaluated"}};
    // «Sin pack» is a selection of its own (RF-1.3).
    const bool has_pack =
        std::any_of(summary.materials.begin(), summary.materials.end(),
                    [](const StoredMaterial &material) { return material.role == "pack"; });
    if (!has_pack)
        document.insert("pack", "none");
    toml::array materials;
    for (const auto &material : summary.materials)
        materials.push_back(toml::table{{"role", material.role},
                                        {"field", material.field},
                                        {"path", material.path},
                                        {"sha256", material.sha256},
                                        {"size", std::to_string(material.size)}});
    document.insert("material", std::move(materials));
    toml::array values;
    for (const auto &value : summary.values)
        values.push_back(toml::table{{"key", value.key},
                                     {"value", value.value},
                                     {"source", std::string{value_source_code(value.source)}}});
    document.insert("effective", std::move(values));
    toml::array takes;
    for (const auto &take : summary.takes)
        takes.push_back(toml::table{{"position", static_cast<std::int64_t>(take.position)},
                                    {"path", take.take},
                                    {"sha256", take.sha256},
                                    {"run_id", take.run_id},
                                    {"outcome", take.outcome},
                                    {"diagnostic", take.diagnostic},
                                    {"playback", take.playback},
                                    {"traversal", take.traversal},
                                    {"evidence", take.evidence},
                                    {"evidence_reasons", string_array(take.evidence_reasons)}});
    document.insert("take", std::move(takes));
    std::ostringstream output;
    output << document << '\n';
    return output.str();
}

RequestSummaryReadResult parse_request_summary(std::string_view text) {
    try {
        const auto document = toml::parse(text);
        const auto version = document["schema_version"].value<std::int64_t>();
        if (!version)
            return RequestSummaryReadError::invalid;
        if (*version != request_summary_schema_version)
            return RequestSummaryReadError::unsupported_version;
        RequestSummaryDocument summary;
        const auto kind = document["kind"].value<std::string>();
        const auto request_id = document["request_id"].value<std::string>();
        const auto conditions_id = document["conditions_id"].value<std::string>();
        const auto run_id = document["run_id"].value<std::string>();
        const auto confirmed = document["confirmed"].value<bool>();
        const auto exit_code = document["exit_code"].value<std::int64_t>();
        const auto linear = document["linear_complete"].value<bool>();
        if (kind != "request_summary" || !request_id || !conditions_id || !run_id || !confirmed ||
            !exit_code || !linear)
            return RequestSummaryReadError::invalid;
        summary.request_id = *request_id;
        summary.conditions_id = *conditions_id;
        summary.run_id = *run_id;
        summary.confirmed = *confirmed;
        summary.exit_code = static_cast<int>(*exit_code);
        summary.linear_complete = *linear;
        if (const auto *materials = document["material"].as_array()) {
            for (const auto &node : *materials) {
                const auto *table = node.as_table();
                if (table == nullptr)
                    return RequestSummaryReadError::invalid;
                StoredMaterial material;
                const auto role = (*table)["role"].value<std::string>();
                const auto field = (*table)["field"].value<std::string>();
                const auto path = (*table)["path"].value<std::string>();
                const auto sha256 = (*table)["sha256"].value<std::string>();
                const auto size = (*table)["size"].value<std::string>();
                if (!role || !field || !path || !sha256 || !size)
                    return RequestSummaryReadError::invalid;
                const auto parsed =
                    std::from_chars(size->data(), size->data() + size->size(), material.size);
                if (parsed.ec != std::errc{} || parsed.ptr != size->data() + size->size())
                    return RequestSummaryReadError::invalid;
                material.role = *role;
                material.field = *field;
                material.path = *path;
                material.sha256 = *sha256;
                summary.materials.push_back(std::move(material));
            }
        }
        if (const auto *values = document["effective"].as_array()) {
            for (const auto &node : *values) {
                const auto *table = node.as_table();
                if (table == nullptr)
                    return RequestSummaryReadError::invalid;
                const auto key = (*table)["key"].value<std::string>();
                const auto value = (*table)["value"].value<std::string>();
                const auto source = (*table)["source"].value<std::string>();
                const auto parsed_source = source ? parse_source(*source) : std::nullopt;
                if (!key || !value || !parsed_source)
                    return RequestSummaryReadError::invalid;
                summary.values.push_back({*key, *value, *parsed_source});
            }
        }
        const auto *takes = document["take"].as_array();
        if (takes == nullptr)
            return RequestSummaryReadError::invalid;
        for (const auto &node : *takes) {
            const auto *table = node.as_table();
            if (table == nullptr)
                return RequestSummaryReadError::invalid;
            StoredTakeSummary take;
            const auto position = (*table)["position"].value<std::int64_t>();
            const auto path = (*table)["path"].value<std::string>();
            const auto sha256 = (*table)["sha256"].value<std::string>();
            const auto take_run = (*table)["run_id"].value<std::string>();
            const auto outcome = (*table)["outcome"].value<std::string>();
            const auto diagnostic = (*table)["diagnostic"].value<std::string>();
            const auto playback = (*table)["playback"].value<std::string>();
            const auto traversal = (*table)["traversal"].value<std::string>();
            const auto evidence = (*table)["evidence"].value<std::string>();
            if (!position || *position < 0 || !path || !sha256 || !take_run || !outcome ||
                !diagnostic || !playback || !traversal || !evidence ||
                !read_strings((*table)["evidence_reasons"], take.evidence_reasons))
                return RequestSummaryReadError::invalid;
            take.position = static_cast<std::size_t>(*position);
            take.take = *path;
            take.sha256 = *sha256;
            take.run_id = *take_run;
            take.outcome = *outcome;
            take.diagnostic = *diagnostic;
            take.playback = *playback;
            take.traversal = *traversal;
            take.evidence = *evidence;
            summary.takes.push_back(std::move(take));
        }
        return summary;
    } catch (const toml::parse_error &) {
        return RequestSummaryReadError::invalid;
    }
}

RequestSummaryReadResult read_request_summary(const std::filesystem::path &path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error))
        return error ? RequestSummaryReadError::unreadable : RequestSummaryReadError::missing;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > max_request_summary_bytes)
        return RequestSummaryReadError::unreadable;
    std::ifstream input{path, std::ios::binary};
    if (!input)
        return RequestSummaryReadError::unreadable;
    const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad())
        return RequestSummaryReadError::unreadable;
    return parse_request_summary(text);
}

DurablePublishResult write_request_summary(const std::filesystem::path &path,
                                           const RequestSummaryDocument &document,
                                           DurablePublicationLimits limits) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return DurablePublishError::invalid_target;
    const auto text = format_request_summary(document);
    return publish_durable_file(path, std::as_bytes(std::span{text.data(), text.size()}), limits);
}

std::optional<CheckTechnicalSummary> technical_summary(const RequestSummaryDocument &document) {
    std::vector<TakeTechnicalResult> results;
    results.reserve(document.takes.size());
    for (const auto &take : document.takes) {
        const auto outcome = parse_outcome(take.outcome);
        if (!outcome)
            return std::nullopt;
        TakeTechnicalResult result;
        result.take_id = take.take;
        result.outcome = *outcome;
        result.diagnostic_code = take.diagnostic;
        result.position = take.position;
        result.playback = take.playback;
        result.traversal = take.traversal;
        result.evidence = take.evidence;
        results.push_back(std::move(result));
    }
    return summarize_check_results(results);
}

} // namespace ayther::audio_qa
