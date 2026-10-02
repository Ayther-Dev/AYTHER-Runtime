#pragma once

#include "hd_initialization.h"

#include <ayther/engine/audio_hd_state.hpp>

#include <optional>
#include <string_view>

namespace ayther::audio_qa {

struct EngineHdStateBundle {
    engine::audio_observation::AudioHdStateHeader header;
    std::optional<engine::audio_observation::AudioHdDetectorWindowsState> detector_windows;
    std::optional<engine::audio_observation::AudioHdVoicesState> voices;
    std::optional<engine::audio_observation::AudioHdRequestsPendingState> requests_pending;
};

[[nodiscard]] HdInitializationSelection
initialize_engine_hd_audio(void *session, std::string_view expected_game_state_identity,
                           const EngineHdStateBundle *supplied_state) noexcept;

} // namespace ayther::audio_qa
