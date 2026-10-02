#pragma once

#include "initial_state.h"
#include "recording_input_source.h"

#include <optional>

namespace ayther::audio_qa {

enum class InitialStatePublicationStatus { pending, acknowledged, failed };

enum class InitialStatePublicationError {
    none,
    invalid_state,
    incomplete_state,
    consumer_unavailable,
    consumer_rejected,
};

using InitialStateConsumer = bool (*)(void *context, const InitialState &state) noexcept;

class InitialStatePublication final {
  public:
    explicit InitialStatePublication(InitialState state);

    [[nodiscard]] bool publish(void *context, InitialStateConsumer consumer) noexcept;
    [[nodiscard]] std::optional<RecordingFrameInput>
    next_input(RecordingInputSource &inputs) noexcept;

    [[nodiscard]] InitialStatePublicationStatus status() const noexcept;
    [[nodiscard]] InitialStatePublicationError error() const noexcept;
    [[nodiscard]] bool input_permitted() const noexcept;
    [[nodiscard]] const InitialState &state() const noexcept;

  private:
    InitialState state_;
    InitialStatePublicationStatus status_{InitialStatePublicationStatus::pending};
    InitialStatePublicationError error_{InitialStatePublicationError::none};
};

} // namespace ayther::audio_qa
