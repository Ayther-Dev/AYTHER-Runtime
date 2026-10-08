#pragma once

#include "input_script.h"
#include "key_router.h"
#include "output_profile.h"
#include "player_config.h"
#include "presentation_controller.h"
#include "presentation_health.h"
#include "qa_window_events.h"
#include "replay_debug_overlay.h"
#include "replay_presentation.h"
#include "runtime_options.h"

#include <SDL3/SDL.h>
#include <ayther/ayther_renderer.h>
#include <ayther/ayther_session.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
    // While closing: only the window close is attended.
    [[nodiscard]] bool poll(bool allow_cancel = true);
    // RF-4, RF-5, RF-6: the window events of this poll, in order.
    void poll_events(std::vector<PresentationEvent> &events);
    // `linear` counts the frame as presented by the linear playback (C1-5); a frame shown in
    // pause or recovered is presented again without counting.
    [[nodiscard]] replay_inspection::VideoResult present(AytherSession &session,
                                                         const FrameView &view,
                                                         std::uint32_t recording_frame,
                                                         bool linear = true);
    // RF-2.6: tries to rebuild the swapchain after a loss.
    [[nodiscard]] bool recover_video();
    [[nodiscard]] bool audio_ready() const noexcept { return !audio_removed_; }
    // BR-153: a scripted action reaches the window as the SDL event a person would cause.
    void deliver(const replay_inspection::ScriptAction &action);
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    // BR-091: what the preparation prewarmed (empty without a pack or with HD off).
    [[nodiscard]] const AytherRenderer::PrewarmReport &prewarm() const noexcept { return prewarm_; }
    // RF-6, RF-7: what the debug overlay draws over the next presented frames.
    void set_debug(bool visible, std::vector<replay_inspection::DebugLine> lines,
                   std::string notice);
    void scroll_debug(replay_inspection::ScrollKey key) noexcept {
        debug_overlay_.scroll(key, debug_lines_.size());
    }
    [[nodiscard]] std::uint64_t debug_frames() const noexcept {
        return debug_overlay_.debug_frames();
    }
    // BR-156 (P-7): the CPU time of the overlay on the last presented frame.
    [[nodiscard]] double last_overlay_ms() const noexcept { return last_overlay_ms_; }
    // BR-156 (P-1): the presenting device and the refresh rate of the window's display.
    [[nodiscard]] std::string device_name() const;
    [[nodiscard]] float refresh_hz() const;
    // RF-7.4: when the image of the last presented frame was composed, before presenting it.
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point>
    composed_at() const noexcept {
        return composed_at_;
    }
    // C3: how the renderer drew the replacements of the last presented frame.
    [[nodiscard]] std::optional<engine::render_observation::DrawReport>
    last_draw_report() const noexcept {
        return draw_report_;
    }

  private:
    [[nodiscard]] bool rebuild(int width, int height);
    void degrade(std::string_view code);
    audio_qa::ReplayPresentation &report_;
    SDL_Window *window_{};
    bool video_initialized_{};
    PresentationController presentation_;
    AytherRenderer renderer_;
    AytherRenderer::PrewarmReport prewarm_;
    AytherLayerStack layers_;
    const AytherLayerStack *active_layers_{};
    const OutputProfile *profile_ = &output_profile_default();
    bool renderer_ready_{};
    bool ready_{};
    bool postprocess_ready_{};
    bool shaders_ = true;
    bool hd_ = true;
    std::uint32_t injected_video_failures_{};
    ReplayDebugOverlay debug_overlay_;
    bool debug_visible_{};
    std::vector<replay_inspection::DebugLine> debug_lines_;
    std::string debug_notice_;
    double last_overlay_ms_{};
    std::optional<std::chrono::steady_clock::time_point> composed_at_;
    std::optional<engine::render_observation::DrawReport> draw_report_;
    bool audio_removed_{};
};

} // namespace ayther::runtime
