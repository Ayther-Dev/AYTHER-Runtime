#pragma once

#include "player_overlay.h"
#include "vulkan_backend/vk_context.h"
#include "vulkan_backend/vk_postprocess.h"
#include "vulkan_backend/vk_swapchain.h"

namespace ayther::runtime {

/// Owns window-side Vulkan objects and fixes their teardown order.
class PresentationController final {
public:
    PresentationController() = default;
    ~PresentationController() { shutdown(); }

    PresentationController(const PresentationController&) = delete;
    PresentationController& operator=(const PresentationController&) = delete;

    [[nodiscard]] VkContext& context() noexcept { return context_; }
    [[nodiscard]] VkSwapchain& swapchain() noexcept { return swapchain_; }
    [[nodiscard]] VkPostProcess& postprocess() noexcept { return postprocess_; }
    [[nodiscard]] ayther::PlayerOverlay& overlay() noexcept { return overlay_; }

    void shutdown() noexcept {
        overlay_.shutdown(context_);
        postprocess_.shutdown(context_);
        swapchain_.shutdown();
        context_.shutdown();
    }

private:
    VkContext context_;
    VkSwapchain swapchain_;
    VkPostProcess postprocess_;
    ayther::PlayerOverlay overlay_;
};

}  // namespace ayther::runtime
