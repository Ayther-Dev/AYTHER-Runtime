#pragma once
// ---------------------------------------------------------------------------
// player_overlay.h — In-game pause overlay for ayther_runtime (M4).
//
// Draws a semi-transparent pause menu over the swapchain after every other
// present pass has finished (CRT post-process or plain blit). Uses Dear ImGui
// 1.92+ with SDL3 + Vulkan backends.
//
// Multi-context safe: creates its own ImGuiContext so it coexists with the
// Lab plugin's SDL_Renderer context (AYTHER_WITH_LAB builds).  Each method
// saves/restores the caller's active context.
//
// ## Layout transition chain (mirrors VkSprite exactly)
//
//   Swapchain in TRANSFER_DST_OPTIMAL  (postprocess/blit output)
//     │  [explicit vkCmdPipelineBarrier: TRANSFER_DST → COLOR_ATTACHMENT]
//     ▼
//   COLOR_ATTACHMENT_OPTIMAL
//     │  [render pass, loadOp=LOAD, storeOp=STORE]
//     ▼
//   TRANSFER_DST_OPTIMAL          ← render pass finalLayout auto-transition
//     │  VkPresent::finalize() unchanged
//     ▼
//   PRESENT_SRC_KHR
//
// ## Lifecycle
//   overlay.init(ctx, swap, window)
//   overlay.rebuild(ctx, swap)   // on SDL_EVENT_WINDOW_RESIZED
//   overlay.shutdown(ctx)        // before vkDeviceWaitIdle + device destruction
//
// ## Per-frame usage
//   overlay.handle_event(e)                        // in the SDL event loop
//   overlay.toggle_pause()                         // on Guide / Escape
//   overlay.render(ctx, cmd, swap, hd_on, running) // after present, before finalize
// ---------------------------------------------------------------------------
#include <vulkan/vulkan.h>
#include <SDL3/SDL_events.h>
#include <cstdint>
#include <vector>

#include "player_config.h"   // #299: preferences edited and persisted by the panel

struct ImGuiContext;
class VkContext;
class VkSwapchain;
class AcquiredFrame;
struct SDL_Window;

namespace ayther {

class AytherSession;

/// Owns the pause-menu Vulkan resources and an independent ImGui context.
///
/// This type is not self-releasing because Vulkan destruction requires the
/// originating `VkContext`. The owner must call shutdown() before that context
/// or its device is destroyed.
class PlayerOverlay {
public:
    PlayerOverlay()  = default;
    ~PlayerOverlay() = default;   // shutdown() must be called explicitly

    PlayerOverlay(const PlayerOverlay&)            = delete;
    PlayerOverlay& operator=(const PlayerOverlay&) = delete;

    /// Create render resources and an independent ImGui context.
    /// @pre `swap` is ready and `window` is a valid borrowed SDL window.
    /// @return `true` when the overlay is ready to render.
    bool init(VkContext& ctx, VkSwapchain& swap, SDL_Window* window);

    /// Recreate framebuffers after a swapchain resize.
    /// @pre init() succeeded and the resized swapchain is ready.
    [[nodiscard]] bool rebuild(VkContext& ctx, VkSwapchain& swap);

    /// Forward an SDL event to this overlay's ImGui context.
    void handle_event(const SDL_Event& e);

    /// Toggle the pause state, typically in response to Escape or Guide.
    void toggle_pause() noexcept { paused_ = !paused_; }
    bool is_paused()    const noexcept { return paused_; }

    /// Record the pause overlay into the current command buffer.
    ///
    /// Call after the game frame reaches the swapchain and before
    /// `VkPresent::finalize`. The menu may update `hd_on` and `running`.
    /// Configuration changes are applied to `session` immediately and marked
    /// dirty, but this function never writes them to disk. Nullable session and
    /// configuration pointers intentionally select the basic pause-menu mode.
    ///
    /// @pre init() succeeded and `cmd` is recording for `swap`'s current image.
    /// @note No operation is recorded while the overlay is not paused.
    void render(VkContext& ctx, const AcquiredFrame& frame,
                bool& hd_on, bool& running,
                AytherSession* session = nullptr,
                PlayerConfig* cfg = nullptr, bool* shaders_on = nullptr);

    /// Consume and return the pending configuration-change flag.
    /// Calling this function twice without a new edit returns `false` the
    /// second time, preventing redundant persistence on every frame.
    bool take_config_dirty() noexcept {
        const bool d = cfg_dirty_; cfg_dirty_ = false; return d;
    }

    /// Destroy all owned Vulkan resources and the overlay ImGui context.
    /// Safe after partial initialization and safe to call more than once.
    void shutdown(VkContext& ctx);

    bool is_ready() const noexcept { return render_pass_ != VK_NULL_HANDLE; }

private:
    bool create_render_pass  (VkContext& ctx, VkFormat fmt);
    [[nodiscard]] bool create_framebuffers(
        VkContext& ctx, VkSwapchain& swap,
        std::vector<VkFramebuffer>& output) const;
    void destroy_framebuffers(VkContext& ctx);

    VkRenderPass               render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    uint32_t fb_w_ = 0, fb_h_ = 0;

    ImGuiContext* overlay_ctx_ = nullptr;   // independent context (multi-context safe)
    bool          paused_      = false;
    bool          imgui_ready_ = false;
    /// A configuration change is awaiting persistence; consumed by
    /// take_config_dirty().
    bool          cfg_dirty_   = false;
};

}  // namespace ayther
