#pragma once

#include <cstdint>

namespace ayther::audio_qa {

enum class ReplayProgressKind {
    request_accepted,
    replay_started,
    input_consumed,
    frame_completed,
    replay_finished,
};

struct ReplayProgressEvent {
    ReplayProgressKind kind{};
    std::uint32_t total_inputs{};
    std::uint32_t inputs_consumed{};
    std::uint32_t frames_completed{};
    std::uint32_t recording_frame{};
    std::uint64_t engine_frame{};
};

using ReplayProgressObserver = void (*)(void *context, const ReplayProgressEvent &event) noexcept;

class ReplayProgressPublisher final {
  public:
    ReplayProgressPublisher(std::uint32_t total_inputs, void *context,
                            ReplayProgressObserver observer) noexcept;

    [[nodiscard]] bool publish_acceptance() noexcept;
    [[nodiscard]] bool publish_start() noexcept;
    [[nodiscard]] bool publish_input(std::uint32_t recording_frame) noexcept;
    [[nodiscard]] bool publish_frame(std::uint32_t recording_frame,
                                     std::uint64_t engine_frame) noexcept;
    [[nodiscard]] bool publish_finish() noexcept;

    [[nodiscard]] bool accepted() const noexcept;
    [[nodiscard]] bool started() const noexcept;
    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] std::uint32_t inputs_consumed() const noexcept;
    [[nodiscard]] std::uint32_t frames_completed() const noexcept;
    [[nodiscard]] std::uint64_t last_engine_frame() const noexcept;

  private:
    enum class State { pending, accepted, started, finished };

    void emit(ReplayProgressKind kind, std::uint32_t recording_frame,
              std::uint64_t engine_frame) const noexcept;

    std::uint32_t total_inputs_{};
    void *context_{};
    ReplayProgressObserver observer_{};
    State state_{State::pending};
    std::uint32_t inputs_consumed_{};
    std::uint32_t frames_completed_{};
    std::uint64_t last_engine_frame_{};
};

} // namespace ayther::audio_qa
