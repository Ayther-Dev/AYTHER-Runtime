#include "vk_swapchain.h"
#include "vulkan_backend/vk_context.h"
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
    vkDeviceWaitIdle(ctx.device());
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
    vkDeviceWaitIdle(device_);

    for (SwapFrame& f : frames_) {
        // Destroying the pool releases its command buffers; freeing them
        // separately would be redundant and fragile if allocation changes.
        if (f.cmd_pool    != VK_NULL_HANDLE) vkDestroyCommandPool(device_, f.cmd_pool, nullptr);
        if (f.fence       != VK_NULL_HANDLE) vkDestroyFence      (device_, f.fence, nullptr);
        if (f.image_ready != VK_NULL_HANDLE) vkDestroySemaphore  (device_, f.image_ready, nullptr);
        if (f.render_done != VK_NULL_HANDLE) vkDestroySemaphore  (device_, f.render_done, nullptr);
        f = SwapFrame{};
    }

    for (VkImageView iv : image_views_)
        if (iv != VK_NULL_HANDLE) vkDestroyImageView(device_, iv, nullptr);
    image_views_.clear();
    images_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
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
    vkWaitForFences(ctx.device(), 1, &f.fence, VK_TRUE, UINT64_MAX);
    vkResetFences  (ctx.device(), 1, &f.fence);

    VkResult r = vkAcquireNextImageKHR(
        ctx.device(), swapchain_, UINT64_MAX,
        f.image_ready, VK_NULL_HANDLE, &image_index_);

    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        std::fprintf(stdout, "[VkSwapchain] Swapchain out of date during acquire\n");
        return false;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "[VkSwapchain] vkAcquireNextImageKHR error %d\n", r);
        return false;
    }

    // Reset + begin command buffer for this frame.
    vkResetCommandPool(ctx.device(), f.cmd_pool, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.cmd, &bi);
    return true;
}

bool VkSwapchain::end_frame(VkContext& ctx) {
    SwapFrame& f = frames_[frame_index_];
    vkEndCommandBuffer(f.cmd);

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

    if (vkQueueSubmit(ctx.graphics_queue(), 1, &si, f.fence) != VK_SUCCESS) {
        std::fprintf(stderr, "[VkSwapchain] vkQueueSubmit failed\n");
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

    VkResult r = vkQueuePresentKHR(ctx.present_queue(), &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        std::fprintf(stdout, "[VkSwapchain] Swapchain out of date — rebuild needed\n");
        // Advance frame index before returning so caller rebuilds cleanly.
        frame_index_ = (frame_index_ + 1) % kMaxFrames;
        return false;   // caller calls rebuild()
    }
    if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[VkSwapchain] vkQueuePresentKHR error %d\n", r);
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
        if (vkCreateImageView(ctx.device(), &info, nullptr, &image_views_[i]) != VK_SUCCESS) {
            std::fprintf(stderr, "[VkSwapchain] vkCreateImageView[%zu] failed\n", i);
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
        if (vkCreateCommandPool(ctx.device(), &pi, nullptr, &f.cmd_pool) != VK_SUCCESS) {
            std::fprintf(stderr, "[VkSwapchain] vkCreateCommandPool[%u] failed\n", i);
            return false;
        }

        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = f.cmd_pool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(ctx.device(), &ai, &f.cmd) != VK_SUCCESS) {
            std::fprintf(stderr, "[VkSwapchain] vkAllocateCommandBuffers[%u] failed\n", i);
            return false;
        }

        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;   // pre-signaled so frame 0 doesn't stall
        if (vkCreateFence(ctx.device(), &fi, nullptr, &f.fence) != VK_SUCCESS) {
            std::fprintf(stderr, "[VkSwapchain] vkCreateFence[%u] failed\n", i);
            return false;
        }

        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(ctx.device(), &si, nullptr, &f.image_ready) != VK_SUCCESS ||
            vkCreateSemaphore(ctx.device(), &si, nullptr, &f.render_done) != VK_SUCCESS) {
            std::fprintf(stderr, "[VkSwapchain] vkCreateSemaphore[%u] failed\n", i);
            return false;
        }
    }
    return true;
}

void VkSwapchain::destroy_swapchain_objects(VkContext& ctx) {
    for (VkImageView iv : image_views_)
        if (iv != VK_NULL_HANDLE)
            vkDestroyImageView(ctx.device(), iv, nullptr);
    image_views_.clear();
    images_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(ctx.device(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}
