#include "model_toml.h"

#include <toml++/toml.hpp>

#include <array>
#include <charconv>
#include <initializer_list>
#include <sstream>
#include <utility>

namespace ayther::audio_qa {
namespace {
constexpr std::array admission_names{"pending", "accepted", "rejected"};
constexpr std::array phase_names{"preparing", "ready", "playing", "closing", "closed"};
constexpr std::array playback_names{"not_started", "in_progress", "natural_end", "cancelled",
                                    "error"};
constexpr std::array evidence_names{"pending", "in_progress", "complete", "incomplete"};
constexpr std::array equivalence_names{"unknown", "verified", "incomplete"};

void require(bool valid, CodecError error = CodecError::invalid_field) {
    if (!valid) {
        throw error;
    }
}
void identity(std::string_view value) {
    require(!value.empty() && value.size() <= max_identity_bytes);
    for (const unsigned char c : value) {
        require(c >= 0x20 && c != 0x7f);
    }
}
std::string text_field(const toml::table &table, std::string_view key) {
    const auto value = table[key].value<std::string>();
    require(value.has_value());
    return *value;
}
std::string identity_field(const toml::table &table, std::string_view key) {
    auto value = text_field(table, key);
    identity(value);
    return value;
}
template <class E, std::size_t N>
const char *enum_name(E value, const std::array<const char *, N> &names) {
    const auto index = static_cast<std::size_t>(value);
    require(index < N);
    return names[index];
}
template <class E, std::size_t N>
E enum_field(const toml::table &table, std::string_view key,
             const std::array<const char *, N> &names) {
    const auto value = text_field(table, key);
    for (std::size_t i = 0; i < N; ++i) {
        if (value == names[i]) {
            return static_cast<E>(i);
        }
    }
    throw CodecError::invalid_field;
}
void document(const toml::table &table, std::string_view kind,
              std::initializer_list<std::string_view> fields) {
    require(table["schema_version"].is_integer() && table["schema_minor"].is_integer());
    require(table["schema_version"].value<std::int64_t>() == 1 &&
                table["schema_minor"].value<std::int64_t>() == 0,
            CodecError::incompatible_version);
    require(text_field(table, "kind") == kind);
    for (const auto &[key, value] : table) {
        bool known = false;
        for (const auto allowed : fields) {
            known = known || key.str() == allowed;
        }
        require(known);
    }
}
std::optional<std::uint64_t> progress(const toml::table &table, std::string_view key) {
    if (!table.contains(key)) {
        return std::nullopt;
    }
    const auto value = text_field(table, key);
    require(!value.empty() && value.front() != '+' && value.front() != '-');
    std::uint64_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size());
    return result;
}
EncodeResult format(const toml::table &table) {
    std::ostringstream stream;
    stream << table;
    auto text = stream.str();
    require(text.size() <= max_metadata_bytes, CodecError::too_large);
    return text;
}
template <class T, class Decoder> DecodeResult<T> parse(std::string_view text, Decoder decode) {
    if (text.size() > max_metadata_bytes) {
        return CodecError::too_large;
    }
    try {
        return decode(toml::parse(text));
    } catch (const toml::parse_error &) {
        return CodecError::malformed_document;
    } catch (CodecError error) {
        return error;
    }
}
} // namespace

EncodeResult to_toml(const Request &request) {
    try {
        identity(request.request_id);
        identity(request.session_id);
        identity(request.conditions_id);
        require(!request.take_ids.empty() && request.take_ids.size() <= max_request_takes);
        toml::array takes;
        for (const auto &id : request.take_ids) {
            identity(id);
            takes.push_back(id);
        }
        toml::table table{{"schema_version", 1},
                          {"schema_minor", 0},
                          {"kind", "request"},
                          {"request_id", request.request_id},
                          {"session_id", request.session_id},
                          {"conditions_id", request.conditions_id},
                          {"take_ids", std::move(takes)},
                          {"admission", enum_name(request.admission, admission_names)}};
        // Spec 002 (contracts.md C1-2): only a 1.1 request carries its language.
        if (request.language) {
            require(*request.language == "es" || *request.language == "en");
            table.insert("language", *request.language);
        }
        return format(table);
    } catch (CodecError error) {
        return error;
    }
}

EncodeResult to_toml(const Run &run) {
    try {
        identity(run.run_id);
        identity(run.request_id);
        identity(run.take_id);
        toml::table table{
            {"schema_version", 1},
            {"schema_minor", 0},
            {"kind", "run"},
            {"run_id", run.run_id},
            {"request_id", run.request_id},
            {"take_id", run.take_id},
            {"phase", enum_name(run.phase, phase_names)},
            {"playback_result", enum_name(run.playback_result, playback_names)},
            {"evidence_result", enum_name(run.evidence_result, evidence_names)},
            {"equivalence_result", enum_name(run.equivalence_result, equivalence_names)},
            {"cessation_confirmed", run.cessation_confirmed}};
        if (run.last_executed_frame) {
            table.insert("last_executed_frame", std::to_string(*run.last_executed_frame));
        }
        if (run.last_durable_sample) {
            table.insert("last_durable_sample", std::to_string(*run.last_durable_sample));
        }
        return format(table);
    } catch (CodecError error) {
        return error;
    }
}

DecodeResult<Request> request_from_toml(std::string_view text) {
    return parse<Request>(text, [](const toml::table &table) {
        document(table, "request",
                 {"schema_version", "schema_minor", "kind", "request_id", "session_id",
                  "conditions_id", "take_ids", "admission", "language"});
        Request request;
        if (const auto *language = table.get("language")) {
            const auto value = language->value<std::string>();
            require(value && (*value == "es" || *value == "en"));
            request.language = *value;
        }
        request.request_id = identity_field(table, "request_id");
        request.session_id = identity_field(table, "session_id");
        request.conditions_id = identity_field(table, "conditions_id");
        request.admission = enum_field<Admission>(table, "admission", admission_names);
        const auto *takes = table["take_ids"].as_array();
        require(takes && !takes->empty() && takes->size() <= max_request_takes);
        for (const auto &entry : *takes) {
            const auto id = entry.value<std::string>();
            require(id.has_value());
            identity(*id);
            request.take_ids.push_back(*id);
        }
        return request;
    });
}

DecodeResult<Run> run_from_toml(std::string_view text) {
    return parse<Run>(text, [](const toml::table &table) {
        document(table, "run",
                 {"schema_version", "schema_minor", "kind", "run_id", "request_id", "take_id",
                  "phase", "playback_result", "evidence_result", "equivalence_result",
                  "last_executed_frame", "last_durable_sample", "cessation_confirmed"});
        Run run;
        run.run_id = identity_field(table, "run_id");
        run.request_id = identity_field(table, "request_id");
        run.take_id = identity_field(table, "take_id");
        run.phase = enum_field<Phase>(table, "phase", phase_names);
        run.playback_result = enum_field<PlaybackResult>(table, "playback_result", playback_names);
        run.evidence_result = enum_field<EvidenceResult>(table, "evidence_result", evidence_names);
        run.equivalence_result =
            enum_field<EquivalenceResult>(table, "equivalence_result", equivalence_names);
        run.last_executed_frame = progress(table, "last_executed_frame");
        run.last_durable_sample = progress(table, "last_durable_sample");
        const auto confirmed = table["cessation_confirmed"].value<bool>();
        require(confirmed.has_value());
        run.cessation_confirmed = *confirmed;
        return run;
    });
}

} // namespace ayther::audio_qa
