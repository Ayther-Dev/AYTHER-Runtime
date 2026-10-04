#include "runtime_protocol_v11.h"

#include "model_limits.h"
#include "protocol_header.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>

namespace ayther::audio_qa {
namespace {

constexpr std::array replay_phases{"preparing",  "playing",     "pausing",      "paused",
                                   "recovering", "interrupted", "ended_paused", "closing"};

constexpr std::array inspection_controls{"pause",       "resume",         "step_forward",
                                         "step_back",   "recover_failed", "overlay_toggle",
                                         "interrupted", "recovered",      "advance_take"};

template <std::size_t N>
bool one_of(const std::array<const char *, N> &values, std::string_view value) {
    return std::any_of(values.begin(), values.end(),
                       [value](const char *candidate) { return value == candidate; });
}

bool offers(const CapabilitySet &offer, std::string_view capability) {
    return std::find(offer.capabilities.begin(), offer.capabilities.end(), capability) !=
           offer.capabilities.end();
}

bool identity(const std::optional<std::string> &value) {
    return value && !value->empty() && value->size() <= max_identity_bytes;
}

std::optional<std::uint64_t> unsigned_field(const Fact &fact, std::string_view name) {
    for (const auto &field : fact.fields) {
        if (field.name != name)
            continue;
        if (field.availability != Availability::known)
            return std::nullopt;
        if (const auto *value = std::get_if<std::uint64_t>(&field.value))
            return *value;
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::string> text_field(const Fact &fact, std::string_view name) {
    for (const auto &field : fact.fields)
        if (field.name == name && field.availability == Availability::known)
            if (const auto *value = std::get_if<std::string>(&field.value))
                return *value;
    return std::nullopt;
}

} // namespace

ProtocolChoice negotiate_runtime_protocol(const CapabilitySet &offer,
                                          std::string_view presentation) {
    const auto &runtime = offer.contracts[static_cast<std::size_t>(ContractKind::runtime)];
    const bool v11 =
        std::find(runtime.begin(), runtime.end(), runtime_protocol_v11) != runtime.end();
    if (presentation == "visible") {
        if (!v11)
            return FieldIssue{"--runtime", "runtime_missing_protocol_1_1"};
        if (!offers(offer, "visible_replay_v1"))
            return FieldIssue{"--runtime", "runtime_missing_visible_replay_v1"};
        if (!offers(offer, "inspection_v1"))
            return FieldIssue{"--runtime", "runtime_missing_inspection_v1"};
        return runtime_protocol_v11;
    }
    if (v11)
        return runtime_protocol_v11;
    if (std::find(runtime.begin(), runtime.end(), runtime_protocol_version) != runtime.end())
        return runtime_protocol_version;
    return FieldIssue{"--runtime", "runtime_incompatible"};
}

SessionStatus decode_session_status(std::span<const std::byte> message,
                                    std::uint64_t expected_sequence) {
    if (message.size() < protocol_header_bytes)
        return ProtocolV11Error::header_rejected;
    const auto header = decode_protocol_header(message.first(protocol_header_bytes), 2U);
    if (header.error != HeaderError::none || header.header.type != MessageType::session_status ||
        header.header.channel_sequence != expected_sequence ||
        header.header.payload_bytes != message.size() - protocol_header_bytes)
        return ProtocolV11Error::header_rejected;
    try {
        const auto payload = message.subspan(protocol_header_bytes);
        const auto document = toml::parse(
            std::string_view{reinterpret_cast<const char *>(payload.data()), payload.size()});
        const auto status = document["status"].value<std::string>();
        const auto run_id = document["run_id"].value<std::string>();
        const auto position = document["take_position"].value<std::int64_t>();
        if (!status || !identity(run_id) || !position || *position < 0)
            return ProtocolV11Error::invalid_payload;
        if (*status == "run_opened") {
            if (document["traversal"].value<std::string>() != "post_end_inspection")
                return ProtocolV11Error::invalid_payload;
            return RunOpened{*run_id, static_cast<std::size_t>(*position)};
        }
        if (*status != "replay_state")
            return ProtocolV11Error::invalid_payload;
        const auto phase = document["phase"].value<std::string>();
        const auto total = document["frames_total"].value<std::int64_t>();
        const auto overlay = document["overlay_visible"].value<bool>();
        if (!phase || !one_of(replay_phases, *phase) || !total || *total < 0 || !overlay)
            return ProtocolV11Error::invalid_payload;
        ReplayStateView state;
        state.run_id = *run_id;
        state.take_position = static_cast<std::size_t>(*position);
        state.phase = *phase;
        state.frames_total = static_cast<std::uint64_t>(*total);
        state.overlay_visible = *overlay;
        if (const auto *frame = document.get("frame")) {
            const auto value = frame->value<std::int64_t>();
            if (!value || *value < 0 || static_cast<std::uint64_t>(*value) >= state.frames_total)
                return ProtocolV11Error::invalid_payload;
            state.frame = static_cast<std::uint64_t>(*value);
        }
        // C1-3: the cause exists only with the interrupted phase.
        const auto cause = document["interruption_cause"].value<std::string>();
        if (cause.has_value() != (state.phase == "interrupted") || (cause && !identity(cause)))
            return ProtocolV11Error::invalid_payload;
        state.interruption_cause = cause.value_or(std::string{});
        return state;
    } catch (const toml::parse_error &) {
        return ProtocolV11Error::invalid_payload;
    }
}

std::optional<InspectionEvent> read_inspection_event(const Fact &fact) {
    if (fact.kind != "inspection_event")
        return std::nullopt;
    const auto seq = unsigned_field(fact, "seq");
    const auto control = text_field(fact, "control");
    const auto before = unsigned_field(fact, "frame_before");
    const auto after = unsigned_field(fact, "frame_after");
    const auto visit = unsigned_field(fact, "visit");
    const auto elapsed = unsigned_field(fact, "elapsed_ms");
    if (!seq || *seq == 0U || !control || !one_of(inspection_controls, *control) || !before ||
        !after || !visit || !elapsed)
        return std::nullopt;
    return InspectionEvent{*seq, *control, *before, *after, *visit, *elapsed};
}

} // namespace ayther::audio_qa
