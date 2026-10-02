#include "capability_report.h"

#include "model_limits.h"

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace ayther::audio_qa {
namespace {

constexpr std::string_view marker{"AYTHER_QA_CAPABILITIES "};

class JsonCursor final {
  public:
    explicit JsonCursor(const std::string_view input) : input_(input) {}

    [[nodiscard]] bool token(const std::string_view expected) noexcept {
        whitespace();
        if (!input_.substr(position_).starts_with(expected))
            return false;
        position_ += expected.size();
        return true;
    }

    [[nodiscard]] std::optional<std::string> string() {
        whitespace();
        if (position_ >= input_.size() || input_[position_++] != '"')
            return std::nullopt;
        std::string result;
        while (position_ < input_.size()) {
            const char character = input_[position_++];
            if (character == '"')
                return result;
            if (static_cast<unsigned char>(character) < 0x20U || character == '\\')
                return std::nullopt;
            result.push_back(character);
            if (result.size() > max_identity_bytes)
                return std::nullopt;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint32_t> number() noexcept {
        whitespace();
        const auto *first = input_.data() + position_;
        const auto *last = input_.data() + input_.size();
        std::uint64_t value{};
        const auto parsed = std::from_chars(first, last, value);
        if (parsed.ec != std::errc{} || parsed.ptr == first ||
            value > (std::numeric_limits<std::uint32_t>::max)())
            return std::nullopt;
        position_ = static_cast<std::size_t>(parsed.ptr - input_.data());
        return static_cast<std::uint32_t>(value);
    }

    [[nodiscard]] bool finished() noexcept {
        whitespace();
        return position_ == input_.size();
    }

  private:
    void whitespace() noexcept {
        while (position_ < input_.size() &&
               (input_[position_] == ' ' || input_[position_] == '\t' ||
                input_[position_] == '\r' || input_[position_] == '\n'))
            ++position_;
    }

    std::string_view input_;
    std::size_t position_{};
};

[[nodiscard]] std::optional<ContractVersion> parse_version(const std::string_view text) noexcept {
    const auto separator = text.find('.');
    if (separator == std::string_view::npos || separator == 0U || separator + 1U == text.size())
        return std::nullopt;
    ContractVersion version;
    const auto major = std::from_chars(text.data(), text.data() + separator, version.major);
    const auto minor =
        std::from_chars(text.data() + separator + 1U, text.data() + text.size(), version.minor);
    if (major.ec != std::errc{} || major.ptr != text.data() + separator ||
        minor.ec != std::errc{} || minor.ptr != text.data() + text.size())
        return std::nullopt;
    return version;
}

[[nodiscard]] bool parse_versions(JsonCursor &cursor, std::vector<ContractVersion> &versions) {
    if (!cursor.token("["))
        return false;
    for (;;) {
        if (cursor.token("]"))
            return !versions.empty();
        const auto text = cursor.string();
        const auto version = text ? parse_version(*text) : std::nullopt;
        if (!version || versions.size() == max_contract_versions)
            return false;
        versions.push_back(*version);
        if (cursor.token("]"))
            return true;
        if (!cursor.token(","))
            return false;
    }
}

[[nodiscard]] bool parse_capabilities(JsonCursor &cursor, std::vector<std::string> &names) {
    if (!cursor.token("["))
        return false;
    if (cursor.token("]"))
        return true;
    for (;;) {
        const auto name = cursor.string();
        if (!name || names.size() == max_capability_names)
            return false;
        names.push_back(*name);
        if (cursor.token("]"))
            return true;
        if (!cursor.token(","))
            return false;
    }
}

[[nodiscard]] bool parse_limit(JsonCursor &cursor, const std::string_view name,
                               std::uint32_t &value, const bool first) {
    if ((!first && !cursor.token(",")) || !cursor.token("\"") || !cursor.token(name) ||
        !cursor.token("\":"))
        return false;
    const auto parsed = cursor.number();
    if (!parsed)
        return false;
    value = *parsed;
    return true;
}

[[nodiscard]] CapabilityReportResult decode_json(const std::string_view json) {
    JsonCursor cursor{json};
    if (!cursor.token("{\"schema\":"))
        return CapabilityReportError::malformed_report;
    const auto schema = cursor.string();
    if (!schema || *schema != "1.0")
        return schema ? CapabilityReportError::incompatible_schema
                      : CapabilityReportError::malformed_report;
    if (!cursor.token(",\"runtime_version\":"))
        return CapabilityReportError::malformed_report;
    const auto runtime_version = cursor.string();
    if (!runtime_version || runtime_version->empty() || !cursor.token(",\"engine_version\":"))
        return CapabilityReportError::malformed_report;
    const auto engine_version = cursor.string();
    if (!engine_version || engine_version->empty() || !cursor.token(",\"contracts\":{"))
        return CapabilityReportError::malformed_report;

    CapabilitySet offer;
    constexpr std::array<std::string_view, 4> contract_names{"engine", "runtime", "evidence",
                                                             "hd_state"};
    for (std::size_t index{}; index < contract_names.size(); ++index) {
        if ((index != 0U && !cursor.token(",")) || !cursor.token("\"") ||
            !cursor.token(contract_names[index]) || !cursor.token("\":"))
            return CapabilityReportError::malformed_report;
        if (!parse_versions(cursor, offer.contracts[index]))
            return CapabilityReportError::malformed_report;
    }
    if (!cursor.token("},\"capabilities\":") || !parse_capabilities(cursor, offer.capabilities) ||
        !cursor.token(",\"limits\":{"))
        return CapabilityReportError::malformed_report;
    if (!parse_limit(cursor, "fact_bytes", offer.limits.fact_bytes, true) ||
        !parse_limit(cursor, "batch_bytes", offer.limits.batch_bytes, false) ||
        !parse_limit(cursor, "live_occurrences", offer.limits.live_occurrences, false) ||
        !parse_limit(cursor, "audio_channels", offer.limits.audio_channels, false) ||
        !parse_limit(cursor, "sample_rate", offer.limits.sample_rate, false) ||
        !parse_limit(cursor, "cancel_milliseconds", offer.limits.cancel_milliseconds, false) ||
        !cursor.token("}}") || !cursor.finished())
        return CapabilityReportError::malformed_report;
    return offer;
}

} // namespace

CapabilityReportResult decode_runtime_capability_report(const std::string_view process_output) {
    const auto marker_at = process_output.find(marker);
    if (marker_at == std::string_view::npos)
        return CapabilityReportError::marker_missing;
    if (process_output.find(marker, marker_at + marker.size()) != std::string_view::npos)
        return CapabilityReportError::duplicate_report;
    const auto json_begin = marker_at + marker.size();
    const auto line_end = process_output.find_first_of("\r\n", json_begin);
    const auto json = process_output.substr(json_begin, line_end == std::string_view::npos
                                                            ? std::string_view::npos
                                                            : line_end - json_begin);
    return decode_json(json);
}

} // namespace ayther::audio_qa
