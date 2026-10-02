#include "replay_execution_result.h"

#include "model_limits.h"
#include "protocol_header.h"
#include "recording_header.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <iterator>
#include <limits>
#include <sstream>
#include <string_view>
#include <toml++/toml.hpp>

namespace ayther::audio_qa {
namespace {

[[nodiscard]] bool identifier(const std::string_view value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}

[[nodiscard]] std::string hex(const std::span<const std::uint8_t> bytes) {
    constexpr std::string_view digits{"0123456789abcdef"};
    std::string result(bytes.size() * 2U, '0');
    for (std::size_t index{}; index < bytes.size(); ++index) {
        result[index * 2U] = digits[bytes[index] >> 4U];
        result[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
    }
    return result;
}

[[nodiscard]] bool parse_hex(const std::string_view text,
                             std::array<std::uint8_t, 32> &value) noexcept {
    if (text.size() != value.size() * 2U)
        return false;
    for (std::size_t index{}; index < value.size(); ++index) {
        unsigned byte{};
        const auto begin = text.data() + index * 2U;
        const auto parsed = std::from_chars(begin, begin + 2U, byte, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != begin + 2U || byte > 0xffU)
            return false;
        value[index] = static_cast<std::uint8_t>(byte);
    }
    return true;
}

[[nodiscard]] toml::table identity_table(const ContentIdentity &identity) {
    return toml::table{{"sha256", hex(identity.sha256)},
                       {"byte_size", static_cast<std::int64_t>(identity.byte_size)}};
}

[[nodiscard]] bool parse_identity(const toml::table *const table,
                                  ContentIdentity &identity) noexcept {
    if (table == nullptr)
        return false;
    if (table->size() != 2U)
        return false;
    const auto digest = (*table)["sha256"].value<std::string>();
    const auto size = (*table)["byte_size"].value<std::int64_t>();
    return digest && size && *size >= 0 && parse_hex(*digest, identity.sha256) &&
           (identity.byte_size = static_cast<std::uint64_t>(*size), true);
}

[[nodiscard]] bool valid_trace_id(const ReplayTraceFactId &id) noexcept {
    return id.producer != 0U && id.sequence != 0U &&
           id.sequence <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
}

[[nodiscard]] toml::table trace_id_table(const ReplayTraceFactId &id) {
    return toml::table{{"producer", static_cast<std::int64_t>(id.producer)},
                       {"sequence", static_cast<std::int64_t>(id.sequence)}};
}

[[nodiscard]] bool parse_trace_id(const toml::table *const table, ReplayTraceFactId &id) noexcept {
    if (table == nullptr || table->size() != 2U)
        return false;
    const auto producer = (*table)["producer"].value<std::int64_t>();
    const auto sequence = (*table)["sequence"].value<std::int64_t>();
    if (!producer || *producer < 0 || *producer > std::numeric_limits<std::uint32_t>::max() ||
        !sequence || *sequence < 0)
        return false;
    id.producer = static_cast<std::uint32_t>(*producer);
    id.sequence = static_cast<std::uint64_t>(*sequence);
    return true;
}

[[nodiscard]] toml::table trace_table(const ReplayTraceSummary &trace) {
    return toml::table{
        {"observed_fact_count", static_cast<std::int64_t>(trace.observed_fact_count)},
        {"ingress", trace_id_table(trace.ingress)},
        {"candidate", trace_id_table(trace.candidate)},
        {"selection", trace_id_table(trace.selection)},
        {"playback_request", trace_id_table(trace.playback_request)},
        {"playback_decision", trace_id_table(trace.playback_decision)},
        {"playback_effect", trace_id_table(trace.playback_effect)},
        {"mix_span", trace_id_table(trace.mix_span)},
        {"occurrence", static_cast<std::int64_t>(trace.occurrence)},
        {"causally_connected", trace.causally_connected},
        {"loss_free", trace.loss_free}};
}

[[nodiscard]] bool parse_trace(const toml::table *const table, ReplayTraceSummary &trace) noexcept {
    if (table == nullptr || table->size() != 11U)
        return false;
    const auto count = (*table)["observed_fact_count"].value<std::int64_t>();
    const auto occurrence = (*table)["occurrence"].value<std::int64_t>();
    const auto connected = (*table)["causally_connected"].value<bool>();
    const auto loss_free = (*table)["loss_free"].value<bool>();
    if (!count || *count < 0 || !occurrence || *occurrence < 0 || !connected || !loss_free ||
        !parse_trace_id((*table)["ingress"].as_table(), trace.ingress) ||
        !parse_trace_id((*table)["candidate"].as_table(), trace.candidate) ||
        !parse_trace_id((*table)["selection"].as_table(), trace.selection) ||
        !parse_trace_id((*table)["playback_request"].as_table(), trace.playback_request) ||
        !parse_trace_id((*table)["playback_decision"].as_table(), trace.playback_decision) ||
        !parse_trace_id((*table)["playback_effect"].as_table(), trace.playback_effect) ||
        !parse_trace_id((*table)["mix_span"].as_table(), trace.mix_span))
        return false;
    trace.observed_fact_count = static_cast<std::uint64_t>(*count);
    trace.occurrence = static_cast<std::uint64_t>(*occurrence);
    trace.causally_connected = *connected;
    trace.loss_free = *loss_free;
    return true;
}

} // namespace

bool well_formed(const ReplayExecutionResult &result) noexcept {
    const auto &p = result.presentation;
    if ((p.mode != "none" && p.mode != "visible") || !identifier(p.code) ||
        p.output_profile.size() > max_identity_bytes ||
        p.audio_backend.size() > max_identity_bytes ||
        p.presented_frames > result.inputs_consumed || p.affected_frames > result.inputs_consumed ||
        (p.affected_frames != 0U && (p.first_affected_frame > p.last_affected_frame ||
                                     p.last_affected_frame >= result.inputs_consumed)) ||
        (result.succeeded &&
         (!p.complete() || (p.mode == "visible" && p.presented_frames != result.inputs_consumed))))
        return false;
    return identifier(result.run_id) && !result.take_id.empty() &&
           result.take_id.size() <= max_reference_value_bytes &&
           result.recording_frames <= max_recording_frames &&
           result.inputs_consumed <= result.recording_frames &&
           result.assignment_count <= max_reference_materials &&
           result.initial_game_state.byte_size <= max_recording_state_bytes &&
           result.final_game_state.byte_size <= max_recording_state_bytes &&
           result.trace.observed_fact_count <= max_replay_trace_facts &&
           result.trace.occurrence <=
               static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) &&
           identifier(result.code) &&
           (!result.succeeded ||
            (result.recording_frames > 0U && result.inputs_consumed == result.recording_frames &&
             result.initial_game_state.byte_size > 0U && result.final_game_state.byte_size > 0U &&
             result.trace.observed_fact_count > 0U && result.trace.loss_free &&
             (result.code == "replay_evidence_streamed" ||
              (result.assignment_count > 0U && result.trace.causally_connected &&
               result.trace.occurrence != 0U && valid_trace_id(result.trace.ingress) &&
               valid_trace_id(result.trace.candidate) && valid_trace_id(result.trace.selection) &&
               valid_trace_id(result.trace.playback_request) &&
               valid_trace_id(result.trace.playback_decision) &&
               valid_trace_id(result.trace.playback_effect) &&
               valid_trace_id(result.trace.mix_span)))));
}

EncodedReplayExecutionResult encode_replay_execution_result(const ReplayExecutionResult &result,
                                                            const std::uint64_t sequence) {
    if (!well_formed(result) || sequence == 0U)
        return ReplayExecutionResultError::invalid_model;
    const auto &p = result.presentation;
    toml::table document{
        {"schema", "1.3"},
        {"presentation", toml::table{{"mode", p.mode},
                                     {"code", p.code},
                                     {"output_profile", p.output_profile},
                                     {"audio_backend", p.audio_backend},
                                     {"initial_width", p.initial_width},
                                     {"initial_height", p.initial_height},
                                     {"hd_enabled", p.hd_enabled},
                                     {"shaders_enabled", p.shaders_enabled},
                                     {"physical_reference_equivalence", "not_verified"},
                                     {"presented_frames", p.presented_frames},
                                     {"affected_frames", p.affected_frames},
                                     {"first_affected_frame", p.first_affected_frame},
                                     {"last_affected_frame", p.last_affected_frame},
                                     {"cancelled", p.cancelled}}},
        {"run_id", result.run_id},
        {"take_id", result.take_id},
        {"recording_frames", static_cast<std::int64_t>(result.recording_frames)},
        {"inputs_consumed", static_cast<std::int64_t>(result.inputs_consumed)},
        {"assignment_count", static_cast<std::int64_t>(result.assignment_count)},
        {"initial_game_state", identity_table(result.initial_game_state)},
        {"final_game_state", identity_table(result.final_game_state)},
        {"trace", trace_table(result.trace)},
        {"succeeded", result.succeeded},
        {"code", result.code}};
    std::ostringstream output;
    output << document;
    const std::string payload = output.str();
    if (payload.empty() || payload.size() > max_protocol_payload_bytes)
        return ReplayExecutionResultError::invalid_model;
    const auto header = encode_protocol_header(
        {MessageType::terminal, static_cast<std::uint32_t>(payload.size()), 2U, sequence});
    std::vector<std::byte> message(header.begin(), header.end());
    message.reserve(header.size() + payload.size());
    std::transform(
        payload.begin(), payload.end(), std::back_inserter(message),
        [](const char value) { return static_cast<std::byte>(static_cast<unsigned char>(value)); });
    return message;
}

DecodedReplayExecutionResult
decode_replay_execution_result(const std::span<const std::byte> message,
                               const std::uint64_t expected_sequence) {
    if (message.size() < protocol_header_bytes)
        return ReplayExecutionResultError::header_rejected;
    const auto decoded = decode_protocol_header(message.first(protocol_header_bytes), 2U);
    if (decoded.error != HeaderError::none)
        return ReplayExecutionResultError::header_rejected;
    if (decoded.header.type != MessageType::terminal)
        return ReplayExecutionResultError::wrong_message_type;
    if (decoded.header.channel_sequence != expected_sequence)
        return ReplayExecutionResultError::sequence_mismatch;
    if (decoded.header.payload_bytes != message.size() - protocol_header_bytes)
        return ReplayExecutionResultError::length_mismatch;
    try {
        const auto payload = message.subspan(protocol_header_bytes);
        const std::string_view text{reinterpret_cast<const char *>(payload.data()), payload.size()};
        const auto document = toml::parse(text);
        const bool legacy = document["schema"].value<std::string>() == "1.2";
        if ((legacy && document.size() != 11U) ||
            (!legacy &&
             (document.size() != 12U || document["schema"].value<std::string>() != "1.3")))
            return ReplayExecutionResultError::invalid_payload;
        ReplayExecutionResult result;
        if (!legacy) {
            const auto *p = document["presentation"].as_table();
            if (!p || p->size() != 14U ||
                (*p)["physical_reference_equivalence"].value<std::string>() != "not_verified")
                return ReplayExecutionResultError::invalid_payload;
            const auto mode = (*p)["mode"].value<std::string>();
            const auto code = (*p)["code"].value<std::string>();
            const auto profile = (*p)["output_profile"].value<std::string>();
            const auto backend = (*p)["audio_backend"].value<std::string>();
            const auto shown = (*p)["presented_frames"].value<std::uint32_t>();
            const auto affected = (*p)["affected_frames"].value<std::uint32_t>();
            const auto first = (*p)["first_affected_frame"].value<std::uint32_t>();
            const auto last = (*p)["last_affected_frame"].value<std::uint32_t>();
            const auto cancelled = (*p)["cancelled"].value<bool>();
            const auto width = (*p)["initial_width"].value<std::uint32_t>();
            const auto height = (*p)["initial_height"].value<std::uint32_t>();
            const auto hd = (*p)["hd_enabled"].value<bool>();
            const auto shaders = (*p)["shaders_enabled"].value<bool>();
            if (!mode || !code || !profile || !backend || !shown || !affected || !first || !last ||
                !cancelled || !width || !height || !hd || !shaders)
                return ReplayExecutionResultError::invalid_payload;
            result.presentation = {*mode, *code,      *profile, *backend, *shown, *affected, *first,
                                   *last, *cancelled, *width,   *height,  *hd,    *shaders};
        }
        const auto run_id = document["run_id"].value<std::string>();
        const auto take_id = document["take_id"].value<std::string>();
        const auto frames = document["recording_frames"].value<std::int64_t>();
        const auto consumed = document["inputs_consumed"].value<std::int64_t>();
        const auto assignment_count = document["assignment_count"].value<std::int64_t>();
        const auto succeeded = document["succeeded"].value<bool>();
        const auto code = document["code"].value<std::string>();
        if (!run_id || !take_id || !frames || *frames < 0 ||
            *frames > std::numeric_limits<std::uint32_t>::max() || !consumed || *consumed < 0 ||
            *consumed > std::numeric_limits<std::uint32_t>::max() || !assignment_count ||
            *assignment_count < 0 || !succeeded || !code ||
            !parse_identity(document["initial_game_state"].as_table(), result.initial_game_state) ||
            !parse_identity(document["final_game_state"].as_table(), result.final_game_state) ||
            !parse_trace(document["trace"].as_table(), result.trace))
            return ReplayExecutionResultError::invalid_payload;
        result.run_id = *run_id;
        result.take_id = *take_id;
        result.recording_frames = static_cast<std::uint32_t>(*frames);
        result.inputs_consumed = static_cast<std::uint32_t>(*consumed);
        result.assignment_count = static_cast<std::uint64_t>(*assignment_count);
        result.succeeded = *succeeded;
        result.code = *code;
        return well_formed(result)
                   ? DecodedReplayExecutionResult{std::move(result)}
                   : DecodedReplayExecutionResult{ReplayExecutionResultError::invalid_payload};
    } catch (const toml::parse_error &) {
        return ReplayExecutionResultError::invalid_payload;
    }
}

} // namespace ayther::audio_qa
