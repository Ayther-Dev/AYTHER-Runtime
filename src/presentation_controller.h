#pragma once

#include "vulkan_backend/vk_context.h"
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

    void shutdown() noexcept {
        swapchain_.shutdown();
        context_.shutdown();
    }

private:
    VkContext context_;
    VkSwapchain swapchain_;
};

}  // namespace ayther::runtime
