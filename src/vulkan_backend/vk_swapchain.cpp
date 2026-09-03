#include "vk_swapchain.h"
#include "vulkan_backend/vk_context.h"
#include "vulkan_backend/vk_result.h"
#include <VkBootstrap.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static VkExtent2D clamp_extent(VkExtent2D requested,
                                const VkSurfaceCapabilitiesKHR& caps) {
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;          // surface dictates size
    return {
        std::clamp(requested.width,  caps.minImageExtent.width,  caps.maxImageExtent.width),
        std::clamp(requested.height, caps.minImageExtent.height, caps.maxImageExtent.height)
    };
}

// ---------------------------------------------------------------------------
// public: init / rebuild / shutdown
// ---------------------------------------------------------------------------
bool VkSwapchain::init(VkContext& ctx, uint32_t w, uint32_t h) {
    device_ = ctx.device();   // Enables context-free shutdown; see the header contract.
    calls_ = ctx.calls();
    if (!create_swapchain(ctx, w, h)) return false;
    if (!create_image_views(ctx))     return false;
    if (!create_sync(ctx))            return false;

    std::fprintf(stdout,
        "[VkSwapchain] Ready  fmt=%d  %ux%u  images=%u  frames_in_flight=%u\n",
        format_, extent_.width, extent_.height,
        image_count(), kMaxFrames);
    return true;
}

bool VkSwapchain::rebuild(VkContext& ctx, uint32_t w, uint32_t h) {
    // Wait for GPU to finish using all resources before tearing down.
    if (const auto failure =
            ctx.wait_idle("vkDeviceWaitIdle [VkSwapchain::rebuild]")) {
        ayther::runtime::vulkan::log_vk_failure(*failure);
        return false;
    }
    destroy_swapchain_objects(ctx);

    if (!create_swapchain(ctx, w, h)) return false;
    if (!create_image_views(ctx))     return false;
    // Sync objects (fences/semaphores) are per-frame, not per-image — keep them.

    std::fprintf(stdout,
        "[VkSwapchain] Rebuilt  %ux%u  images=%u\n",
        extent_.width, extent_.height, image_count());
    return true;
}

void VkSwapchain::shutdown() {
    // The device cached by init() enables complete teardown before
    // vkDestroyDevice (#415).
    if (device_ == VK_NULL_HANDLE) return;   // Never initialized or already closed.

    // No owned resource may remain in flight while it is being destroyed.
    if (const auto failure = ayther::runtime::vulkan::vk_failure(
            "vkDeviceWaitIdle [VkSwapchain::shutdown]",
            calls_.device_wait_idle(device_))) {
        ayther::runtime::vulkan::log_vk_failure(*failure);
    }

    for (SwapFrame& f : frames_) {
        // Destroying the pool releases its command buffers; freeing them
        // separately would be redundant and fragile if allocation changes.
        if (f.cmd_pool != VK_NULL_HANDLE)
            calls_.destroy_command_pool(device_, f.cmd_pool, nullptr);
        if (f.fence != VK_NULL_HANDLE)
            calls_.destroy_fence(device_, f.fence, nullptr);
        if (f.image_ready != VK_NULL_HANDLE)
            calls_.destroy_semaphore(device_, f.image_ready, nullptr);
        if (f.render_done != VK_NULL_HANDLE)
            calls_.destroy_semaphore(device_, f.render_done, nullptr);
        f = SwapFrame{};
    }

    for (VkImageView iv : image_views_)
        if (iv != VK_NULL_HANDLE)
            calls_.destroy_image_view(device_, iv, nullptr);
    image_views_.clear();
    images_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        calls_.destroy_swapchain(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;   // Sentinel: repeated shutdown is a no-op.
}

// ---------------------------------------------------------------------------
// public: per-frame
// ---------------------------------------------------------------------------
bool VkSwapchain::begin_frame(VkContext& ctx) {
    SwapFrame& f = frames_[frame_index_];

    // Wait for this frame's previous submission to finish.
    if (!ayther::runtime::vulkan::require_vk_success(
            "vkWaitForFences",
            ctx.calls().wait_for_fences(
                ctx.device(), 1, &f.fence, VK_TRUE, UINT64_MAX))) {
        return false;
    }

    const VkResult acquire_result = ctx.calls().acquire_next_image(
        ctx.device(), swapchain_, UINT64_MAX,
        f.image_ready, VK_NULL_HANDLE, &image_index_);

    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
        ayther::runtime::vulkan::log_vk_failure(
            {"vkAcquireNextImageKHR", acquire_result});
        return false;
    }
    if (acquire_result != VK_SUCCESS &&
        acquire_result != VK_SUBOPTIMAL_KHR) {
        ayther::runtime::vulkan::log_vk_failure(
            {"vkAcquireNextImageKHR", acquire_result});
        return false;
    }

    if (!ayther::runtime::vulkan::require_vk_success(
            "vkResetFences",
            ctx.calls().reset_fences(ctx.device(), 1, &f.fence))) {
        return false;
    }

    // Reset + begin command buffer for this frame.
    if (!ayther::runtime::vulkan::require_vk_success(
            "vkResetCommandPool",
            ctx.calls().reset_command_pool(ctx.device(), f.cmd_pool, 0))) {
        return false;
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    return ayther::runtime::vulkan::require_vk_success(
        "vkBeginCommandBuffer", ctx.calls().begin_command_buffer(f.cmd, &bi));
}

bool VkSwapchain::end_frame(VkContext& ctx) {
    SwapFrame& f = frames_[frame_index_];
    if (!ayther::runtime::vulkan::require_vk_success(
            "vkEndCommandBuffer", ctx.calls().end_command_buffer(f.cmd))) {
        return false;
    }

    // Submit
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &f.image_ready;
    si.pWaitDstStageMask    = &wait_stage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &f.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &f.render_done;

    if (!ayther::runtime::vulkan::require_vk_success(
            "vkQueueSubmit",
            ctx.calls().queue_submit(
                ctx.graphics_queue(), 1, &si, f.fence))) {
        return false;
    }

    // Present
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &f.render_done;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &swapchain_;
    pi.pImageIndices      = &image_index_;

    const VkResult present_result =
        ctx.calls().queue_present(ctx.present_queue(), &pi);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR ||
        present_result == VK_SUBOPTIMAL_KHR) {
        ayther::runtime::vulkan::log_vk_failure(
            {"vkQueuePresentKHR", present_result});
        // Advance frame index before returning so caller rebuilds cleanly.
        frame_index_ = (frame_index_ + 1) % kMaxFrames;
        return false;   // caller calls rebuild()
    }
    if (!ayther::runtime::vulkan::require_vk_success(
            "vkQueuePresentKHR", present_result)) {
        return false;
    }

    frame_index_ = (frame_index_ + 1) % kMaxFrames;
    return true;
}

// ---------------------------------------------------------------------------
// private helpers
// ---------------------------------------------------------------------------
bool VkSwapchain::create_swapchain(VkContext& ctx, uint32_t w, uint32_t h) {
    // Query surface capabilities for format + present mode selection.
    vkb::SwapchainBuilder builder(
        ctx.physical_device(),
        ctx.device(),
        ctx.surface(),
        ctx.graphics_family(),
        ctx.present_family()
    );

    builder
        .set_desired_format({ VK_FORMAT_B8G8R8A8_UNORM,
                              VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .add_fallback_format({ VK_FORMAT_R8G8B8A8_UNORM,
                               VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)   // vsync
        .set_desired_extent(w, h)
        .set_image_usage_flags(
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT)    // needed for blit from texture
        .set_image_array_layer_count(1)
        .set_clipped(true);

    // Re-use previous swapchain for faster recreation if one exists.
    if (swapchain_ != VK_NULL_HANDLE)
        builder.set_old_swapchain(swapchain_);

    auto result = builder.build();
    if (!result) {
        std::fprintf(stderr, "[VkSwapchain] SwapchainBuilder failed: %s\n",
                     result.error().message().c_str());
        return false;
    }

    vkb::Swapchain vkbs = result.value();
    swapchain_   = vkbs.swapchain;
    format_      = vkbs.image_format;
    color_space_ = vkbs.color_space;
    extent_      = vkbs.extent;
    images_      = vkbs.get_images().value();

    return true;
}

bool VkSwapchain::create_image_views(VkContext& ctx) {
    image_views_.resize(images_.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo info{};
        info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image    = images_[i];
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format   = format_;
        info.components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY
        };
        info.subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
        };
        if (!ayther::runtime::vulkan::require_vk_success(
                "vkCreateImageView [VkSwapchain]",
                ctx.calls().create_image_view(
                    ctx.device(), &info, nullptr, &image_views_[i]))) {
            return false;
        }
    }
    return true;
}

bool VkSwapchain::create_sync(VkContext& ctx) {
    for (uint32_t i = 0; i < kMaxFrames; ++i) {
        SwapFrame& f = frames_[i];

        VkCommandPoolCreateInfo pi{};
        pi.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pi.queueFamilyIndex = ctx.graphics_family();
        if (!ayther::runtime::vulkan::require_vk_success(
                "vkCreateCommandPool",
                ctx.calls().create_command_pool(
                    ctx.device(), &pi, nullptr, &f.cmd_pool))) {
            return false;
        }

        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = f.cmd_pool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (!ayther::runtime::vulkan::require_vk_success(
                "vkAllocateCommandBuffers",
                ctx.calls().allocate_command_buffers(
                    ctx.device(), &ai, &f.cmd))) {
            return false;
        }

        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;   // pre-signaled so frame 0 doesn't stall
        if (!ayther::runtime::vulkan::require_vk_success(
                "vkCreateFence",
                ctx.calls().create_fence(
                    ctx.device(), &fi, nullptr, &f.fence))) {
            return false;
        }

        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (!ayther::runtime::vulkan::require_vk_success(
                "vkCreateSemaphore [image-ready]",
                ctx.calls().create_semaphore(
                    ctx.device(), &si, nullptr, &f.image_ready))) {
            return false;
        }
        if (!ayther::runtime::vulkan::require_vk_success(
                "vkCreateSemaphore [render-done]",
                ctx.calls().create_semaphore(
                    ctx.device(), &si, nullptr, &f.render_done))) {
            return false;
        }
    }
    return true;
}

void VkSwapchain::destroy_swapchain_objects(VkContext& ctx) {
    for (VkImageView iv : image_views_)
        if (iv != VK_NULL_HANDLE)
            ctx.calls().destroy_image_view(ctx.device(), iv, nullptr);
    image_views_.clear();
    images_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        ctx.calls().destroy_swapchain(ctx.device(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}
