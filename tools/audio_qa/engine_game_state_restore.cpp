#include "engine_game_state_restore.h"

#include <ayther/ayther_session.h>

#include <string_view>

namespace ayther::audio_qa {
namespace {

[[nodiscard]] std::string_view error_code(const ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::Ok:
        return "ok";
    case ErrorCode::NotFound:
        return "not_found";
    case ErrorCode::BadFormat:
        return "bad_format";
    case ErrorCode::BadSignature:
        return "bad_signature";
    case ErrorCode::Io:
        return "io_error";
    case ErrorCode::Unsupported:
        return "unsupported";
    case ErrorCode::Internal:
        return "internal_error";
    }
    return "unknown_engine_error";
}

} // namespace

GameStateRestoreOperationResult restore_engine_game_state(void *const session,
                                                          const std::vector<std::uint8_t> &state) {
    if (session == nullptr) {
        return {false, "engine_session_missing",
                "engine session is unavailable for game state restore"};
    }
    auto &engine_session = *static_cast<AytherSession *>(session);
    const auto restored = engine_session.unserialize(state);
    if (restored) {
        return {true, "ok", {}};
    }
    return {false, std::string{error_code(restored.error.code)}, restored.error.message};
}

} // namespace ayther::audio_qa
