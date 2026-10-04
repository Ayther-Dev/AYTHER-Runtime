#pragma once

#include "debug_view.h"

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

struct ImGuiContext;
class VkContext;
class VkSwapchain;
class AcquiredFrame;
struct SDL_Window;

namespace ayther::runtime {

// Spec 002, plan §3 and §5.9 (RF-6, RF-7; BR-144, BR-145): the Dear ImGui adapter of the debug
// record of the replay window. It only lays out the lines of debug_view; it never receives the
// keyboard, so Space, the arrows and I always belong to the inspection (D15). While hidden it
// records nothing but the notice of the inspection, if any.
class ReplayDebugOverlay final {
  public:
    ReplayDebugOverlay() = default;
    ReplayDebugOverlay(const ReplayDebugOverlay &) = delete;
    ReplayDebugOverlay &operator=(const ReplayDebugOverlay &) = delete;

    [[nodiscard]] bool init(VkContext &context, VkSwapchain &swapchain, SDL_Window *window);
    [[nodiscard]] bool rebuild(VkContext &context, VkSwapchain &swapchain);
    void shutdown(VkContext &context);
    [[nodiscard]] bool ready() const noexcept { return imgui_ready_; }

    // Records the overlay after the frame reached the swapchain and before it is finalized.
    void render(const AcquiredFrame &frame, bool visible,
                const std::vector<replay_inspection::DebugLine> &lines, const std::string &notice);
    void scroll(replay_inspection::ScrollKey key, std::size_t total_lines) noexcept;
    void reset_scroll() noexcept { scroll_.reset(); }
    // Frames on which debug data was drawn; never while hidden (BR-144).
    [[nodiscard]] std::uint64_t debug_frames() const noexcept { return debug_frames_; }

  private:
    [[nodiscard]] bool create_render_pass(VkContext &context, VkFormat format);
    [[nodiscard]] bool create_framebuffers(VkContext &context, VkSwapchain &swapchain,
                                           std::vector<VkFramebuffer> &output) const;
    void destroy_framebuffers(VkContext &context);

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    std::uint32_t width_{};
    std::uint32_t height_{};
    ImGuiContext *context_{};
    bool imgui_ready_{};
    replay_inspection::DebugScroll scroll_;
    std::size_t visible_lines_{20};
    std::uint64_t debug_frames_{};
};

} // namespace ayther::runtime
