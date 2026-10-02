#pragma once

#include "output_profile.h"
#include "player_config.h"
#include "presentation_controller.h"
#include "replay_presentation.h"
#include "runtime_options.h"

#include <SDL3/SDL.h>
#include <ayther/ayther_renderer.h>
#include <ayther/ayther_session.h>

namespace ayther::runtime {

// Presentation consumes a borrowed frame; it never steps or supplies game input.
class AudioQaPresentation final {
  public:
    explicit AudioQaPresentation(audio_qa::ReplayPresentation &report) noexcept : report_(report) {}
    ~AudioQaPresentation();
    AudioQaPresentation(const AudioQaPresentation &) = delete;
    AudioQaPresentation &operator=(const AudioQaPresentation &) = delete;

    void initialize(AytherSession &session, const RuntimeOptions &options,
                    const PlayerConfig &config);
    [[nodiscard]] bool poll(bool allow_cancel = true);
    void present(AytherSession &session, const FrameView &view, std::uint32_t recording_frame);

  private:
    [[nodiscard]] bool rebuild(int width, int height);
    void degrade(std::string_view code);
    audio_qa::ReplayPresentation &report_;
    SDL_Window *window_{};
    bool video_initialized_{};
    PresentationController presentation_;
    AytherRenderer renderer_;
    AytherLayerStack layers_;
    const AytherLayerStack *active_layers_{};
    const OutputProfile *profile_ = &output_profile_default();
    bool renderer_ready_{};
    bool ready_{};
    bool postprocess_ready_{};
    bool shaders_ = true;
    bool hd_ = true;
};

} // namespace ayther::runtime
