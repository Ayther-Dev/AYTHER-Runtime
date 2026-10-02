#include "engine_hd_initialization.h"

#include <ayther/ayther_session.h>
#include <ayther/engine/audio_initial_snapshot.hpp>

namespace ayther::audio_qa {
namespace {

namespace observation = engine::audio_observation;

struct EngineHdInitializationContext {
    AytherSession *session{};
    std::string_view expected_game_state_identity;
    const EngineHdStateBundle *supplied_state{};
};

[[nodiscard]] std::string_view
validation_code(const observation::AudioHdStateValidationCode code) noexcept {
    switch (code) {
    case observation::AudioHdStateValidationCode::compatible:
        return "compatible";
    case observation::AudioHdStateValidationCode::unsupported_version:
        return "unsupported_version";
    case observation::AudioHdStateValidationCode::missing_expected_identity:
        return "missing_expected_identity";
    case observation::AudioHdStateValidationCode::missing_identity:
        return "missing_identity";
    case observation::AudioHdStateValidationCode::identity_too_long:
        return "identity_too_long";
    case observation::AudioHdStateValidationCode::identity_mismatch:
        return "identity_mismatch";
    case observation::AudioHdStateValidationCode::frame_mismatch:
        return "frame_mismatch";
    case observation::AudioHdStateValidationCode::missing_sections:
        return "missing_sections";
    case observation::AudioHdStateValidationCode::unknown_sections:
        return "unknown_sections";
    }
    return "unknown_validation_error";
}

[[nodiscard]] std::string_view restore_code(const observation::AudioHdRestoreCode code) noexcept {
    switch (code) {
    case observation::AudioHdRestoreCode::restored:
        return "restored";
    case observation::AudioHdRestoreCode::incompatible_header:
        return "incompatible_header";
    case observation::AudioHdRestoreCode::incomplete_payload:
        return "incomplete_payload";
    case observation::AudioHdRestoreCode::invalid_detector:
        return "invalid_detector";
    case observation::AudioHdRestoreCode::invalid_window:
        return "invalid_window";
    case observation::AudioHdRestoreCode::invalid_pcm:
        return "invalid_pcm";
    case observation::AudioHdRestoreCode::invalid_voice:
        return "invalid_voice";
    case observation::AudioHdRestoreCode::invalid_request:
        return "invalid_request";
    case observation::AudioHdRestoreCode::invalid_pending_audio:
        return "invalid_pending_audio";
    case observation::AudioHdRestoreCode::duplicate_value:
        return "duplicate_value";
    case observation::AudioHdRestoreCode::resource_limit:
        return "resource_limit";
    }
    return "unknown_restore_error";
}

[[nodiscard]] HdInitializationOperationResult prepare_fresh(void *const value) noexcept {
    auto &context = *static_cast<EngineHdInitializationContext *>(value);
    if (context.session == nullptr) {
        return {false, "engine_session_missing",
                "engine session is unavailable for fresh HD initialization"};
    }
    if (!context.session->prepare_fresh_hd_audio()) {
        return {false, "fresh_hd_initialization_failed",
                "Engine could not remove inherited HD audio"};
    }
    const auto snapshot = context.session->audio_initial_snapshot();
    const bool verified = snapshot.complete &&
                          snapshot.initialization == observation::AudioInitializationMode::fresh &&
                          snapshot.voices.empty() && snapshot.windows.empty() &&
                          snapshot.requests.empty() && snapshot.fired_requests.empty() &&
                          snapshot.pending_audio.empty();
    return verified ? HdInitializationOperationResult{true, "fresh", {}}
                    : HdInitializationOperationResult{
                          false, "fresh_hd_state_not_observed",
                          "Engine snapshot does not prove an empty fresh HD state"};
}

[[nodiscard]] HdInitializationOperationResult restore_supplied(void *const value) noexcept {
    auto &context = *static_cast<EngineHdInitializationContext *>(value);
    if (context.session == nullptr) {
        return {false, "engine_session_missing",
                "engine session is unavailable for HD state restore"};
    }
    if (context.supplied_state == nullptr ||
        !context.supplied_state->detector_windows.has_value() ||
        !context.supplied_state->voices.has_value() ||
        !context.supplied_state->requests_pending.has_value()) {
        return {false, "incomplete_payload", "supplied HD state is missing a required payload"};
    }

    const auto &supplied = *context.supplied_state;
    const auto before = context.session->audio_initial_snapshot();
    const auto validation = observation::validate_audio_hd_state(
        supplied.header, context.expected_game_state_identity, before.emulation_frame);
    if (!validation.compatible()) {
        return {false, validation_code(validation.code),
                "supplied HD state header is incompatible"};
    }

    const auto detector = context.session->restore_audio_hd_detector_windows(
        supplied.header, context.expected_game_state_identity, *supplied.detector_windows);
    if (!detector.restored()) {
        return {false, restore_code(detector.code), "detector_windows"};
    }
    const auto voices = context.session->restore_audio_hd_voices(
        supplied.header, context.expected_game_state_identity, *supplied.voices);
    if (!voices.restored()) {
        return {false, restore_code(voices.code), "voices"};
    }
    const auto requests = context.session->restore_audio_hd_requests_pending(
        supplied.header, context.expected_game_state_identity, *supplied.requests_pending);
    if (!requests.restored()) {
        return {false, restore_code(requests.code), "requests_pending"};
    }

    const auto after = context.session->audio_initial_snapshot();
    if (!after.complete || after.initialization != observation::AudioInitializationMode::restored) {
        return {false, "restored_hd_state_not_observed",
                "Engine snapshot does not prove the restored HD state"};
    }
    return {true, "restored", {}};
}

} // namespace

HdInitializationSelection
initialize_engine_hd_audio(void *const session, const std::string_view expected_game_state_identity,
                           const EngineHdStateBundle *const supplied_state) noexcept {
    EngineHdInitializationContext context{static_cast<AytherSession *>(session),
                                          expected_game_state_identity, supplied_state};
    return select_hd_initialization(supplied_state == nullptr ? HdStateAvailability::absent
                                                              : HdStateAvailability::supplied,
                                    {&context, restore_supplied, prepare_fresh});
}

} // namespace ayther::audio_qa
