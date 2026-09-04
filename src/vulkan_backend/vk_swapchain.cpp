#include "vulkan_backend/vk_swapchain.h"

#include "vulkan_backend/vk_context.h"
#include "vulkan_backend/vk_result.h"

#include <VkBootstrap.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

AcquiredFrame::AcquiredFrame(
    VkSwapchain* owner, const std::uint64_t generation,
    const std::uint64_t serial, const std::uint32_t frame_slot,
    const std::uint32_t image_index, const SwapFrame& frame,
    const VkImage image, const VkImageView image_view,
    const VkExtent2D extent) noexcept
    : owner_(owner),
      generation_(generation),
      serial_(serial),
      frame_slot_(frame_slot),
      image_index_(image_index),
      command_buffer_(frame.cmd),
      image_(image),
      image_view_(image_view),
      extent_(extent),
      fence_(frame.fence),
      image_ready_(frame.image_ready),
      render_done_(frame.render_done) {}

AcquiredFrame::AcquiredFrame(AcquiredFrame&& source) noexcept
    : owner_(source.owner_),
      generation_(source.generation_),
      serial_(source.serial_),
      frame_slot_(source.frame_slot_),
      image_index_(source.image_index_),
      command_buffer_(source.command_buffer_),
      image_(source.image_),
      image_view_(source.image_view_),
      extent_(source.extent_),
      fence_(source.fence_),
      image_ready_(source.image_ready_),
      render_done_(source.render_done_) {
    source.invalidate();
}

bool AcquiredFrame::valid() const noexcept {
    return owner_ != nullptr && owner_->accepts(*this);
}

std::optional<VkFramebuffer> AcquiredFrame::framebuffer(
    const std::span<const VkFramebuffer> framebuffers) const noexcept {
    if (!valid() || image_index_ >= framebuffers.size() ||
        framebuffers[image_index_] == VK_NULL_HANDLE) {
        return std::nullopt;
    }
    return framebuffers[image_index_];
}

void AcquiredFrame::invalidate() noexcept {
    owner_ = nullptr;
    generation_ = 0;
    serial_ = 0;
    command_buffer_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    image_view_ = VK_NULL_HANDLE;
    fence_ = VK_NULL_HANDLE;
    image_ready_ = VK_NULL_HANDLE;
    render_done_ = VK_NULL_HANDLE;
}

bool VkSwapchain::init(VkContext& ctx, const std::uint32_t width,
                       const std::uint32_t height) {
    if (is_ready()) {
        return true;
    }
    if (!ctx.is_ready() || width == 0 || height == 0) {
        return false;
    }

    OwnedState pending;
    if (!create_swapchain(ctx, width, height, VK_NULL_HANDLE, pending) ||
        !create_image_views(ctx, pending) || !create_sync(ctx, pending)) {
        destroy_owned(ctx.device(), ctx.calls(), pending);
        return false;
    }

    // A stale partial state, if any, is released only after the replacement is
    // complete. Normal initialization reaches this with an empty state.
    shutdown();
    device_ = ctx.device();
    calls_ = ctx.calls();
    state_ = std::move(pending);
    frame_index_ = 0;
    active_serial_ = 0;
    ++generation_;

    std::fprintf(stdout,
        "[VkSwapchain] Ready fmt=%d %ux%u images=%u frames_in_flight=%u\n",
        state_.format, state_.extent.width, state_.extent.height,
        image_count(), kMaxFrames);
    return true;
}

bool VkSwapchain::rebuild(VkContext& ctx, const std::uint32_t width,
                          const std::uint32_t height) {
    if (!is_ready() || active_serial_ != 0 || width == 0 || height == 0) {
        return false;
    }
    if (const auto failure =
            ctx.wait_idle("vkDeviceWaitIdle [VkSwapchain::rebuild]")) {
        ayther::runtime::vulkan::log_vk_failure(*failure);
        return false;
    }

    OwnedState pending;
    if (!create_swapchain(ctx, width, height, state_.swapchain, pending) ||
        !create_image_views(ctx, pending) || !create_sync(ctx, pending)) {
        destroy_owned(ctx.device(), ctx.calls(), pending);
        return false;
    }

    destroy_owned(device_, calls_, state_);
    state_ = std::move(pending);
    frame_index_ = 0;
    active_serial_ = 0;
    ++generation_;

    std::fprintf(stdout, "[VkSwapchain] Rebuilt %ux%u images=%u\n",
                 state_.extent.width, state_.extent.height, image_count());
    return true;
}

void VkSwapchain::shutdown() noexcept {
    active_serial_ = 0;
    ++generation_;
    frame_index_ = 0;
    if (device_ == VK_NULL_HANDLE) {
        state_ = {};
        return;
    }

    if (const auto failure = ayther::runtime::vulkan::vk_failure(
            "vkDeviceWaitIdle [VkSwapchain::shutdown]",
            calls_.device_wait_idle(device_))) {
        ayther::runtime::vulkan::log_vk_failure(*failure);
    }
    destroy_owned(device_, calls_, state_);
    device_ = VK_NULL_HANDLE;
}

std::optional<AcquiredFrame> VkSwapchain::begin_frame(VkContext& ctx) {
    if (!is_ready() || active_serial_ != 0 ||
        frame_index_ >= state_.frames.size()) {
        return std::nullopt;
    }
    const SwapFrame& frame = state_.frames[frame_index_];
    if (frame.fence == VK_NULL_HANDLE || frame.cmd_pool == VK_NULL_HANDLE ||
        frame.cmd == VK_NULL_HANDLE || frame.image_ready == VK_NULL_HANDLE ||
        frame.render_done == VK_NULL_HANDLE) {
        return std::nullopt;
    }

    if (!ayther::runtime::vulkan::require_vk_success(
            "vkWaitForFences", ctx.calls().wait_for_fences(
                ctx.device(), 1, &frame.fence, VK_TRUE, UINT64_MAX))) {
        return std::nullopt;
    }

    std::uint32_t image_index = 0;
    const VkResult acquire_result = ctx.calls().acquire_next_image(
        ctx.device(), state_.swapchain, UINT64_MAX, frame.image_ready,
        VK_NULL_HANDLE, &image_index);
    if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
        ayther::runtime::vulkan::log_vk_failure(
            {"vkAcquireNextImageKHR", acquire_result});
        return std::nullopt;
    }
    if (image_index >= state_.images.size() ||
        image_index >= state_.image_views.size()) {
        std::fprintf(stderr,
                     "[VkSwapchain] vkAcquireNextImageKHR returned invalid "
                     "image index %u (count=%zu)\n",
                     image_index, state_.images.size());
        return std::nullopt;
    }

    if (!ayther::runtime::vulkan::require_vk_success(
            "vkResetCommandPool", ctx.calls().reset_command_pool(
                ctx.device(), frame.cmd_pool, 0))) {
        return std::nullopt;
    }
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!ayther::runtime::vulkan::require_vk_success(
            "vkBeginCommandBuffer",
            ctx.calls().begin_command_buffer(frame.cmd, &begin_info))) {
        return std::nullopt;
    }

    const std::uint64_t serial = next_serial_++;
    active_serial_ = serial;
    return AcquiredFrame(this, generation_, serial, frame_index_, image_index,
                         frame, state_.images[image_index],
                         state_.image_views[image_index], state_.extent);
}

bool VkSwapchain::end_frame(VkContext& ctx, AcquiredFrame& token) {
    if (!accepts(token)) {
        return false;
    }

    const VkCommandBuffer command_buffer = token.command_buffer_;
    const VkFence fence = token.fence_;
    const VkSemaphore image_ready = token.image_ready_;
    const VkSemaphore render_done = token.render_done_;
    const std::uint32_t image_index = token.image_index_;
    const std::uint32_t frame_slot = token.frame_slot_;

    // From this point onward the capability is consumed, even if a Vulkan call
    // fails. Reusing a partially submitted frame is never legal.
    active_serial_ = 0;
    token.invalidate();

    if (!ayther::runtime::vulkan::require_vk_success(
            "vkEndCommandBuffer", ctx.calls().end_command_buffer(command_buffer))) {
        return false;
    }
    // Reset only immediately before submission. An abandoned/recording-failed
    // token leaves the previously signaled fence reusable.
    if (!ayther::runtime::vulkan::require_vk_success(
            "vkResetFences",
            ctx.calls().reset_fences(ctx.device(), 1, &fence))) {
        return false;
    }

    const VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &image_ready;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_done;
    if (!ayther::runtime::vulkan::require_vk_success(
            "vkQueueSubmit", ctx.calls().queue_submit(
                ctx.graphics_queue(), 1, &submit, fence))) {
        return false;
    }

    const VkSwapchainKHR swapchain = state_.swapchain;
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_done;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &image_index;
    const VkResult present_result =
        ctx.calls().queue_present(ctx.present_queue(), &present);

    frame_index_ = (frame_slot + 1) % kMaxFrames;
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR ||
        present_result == VK_SUBOPTIMAL_KHR) {
        ayther::runtime::vulkan::log_vk_failure(
            {"vkQueuePresentKHR", present_result});
        return false;
    }
    return ayther::runtime::vulkan::require_vk_success(
        "vkQueuePresentKHR", present_result);
}

bool VkSwapchain::create_swapchain(
    VkContext& ctx, const std::uint32_t width, const std::uint32_t height,
    const VkSwapchainKHR old_swapchain, OwnedState& output) {
    vkb::SwapchainBuilder builder(ctx.physical_device(), ctx.device(),
                                  ctx.surface(), ctx.graphics_family(),
                                  ctx.present_family());
    builder
        .set_desired_format({VK_FORMAT_B8G8R8A8_UNORM,
                             VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .add_fallback_format({VK_FORMAT_R8G8B8A8_UNORM,
                              VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .set_image_array_layer_count(1)
        .set_clipped(true);
    if (old_swapchain != VK_NULL_HANDLE) {
        builder.set_old_swapchain(old_swapchain);
    }

    auto result = builder.build();
    if (!result) {
        std::fprintf(stderr, "[VkSwapchain] SwapchainBuilder failed: %s\n",
                     result.error().message().c_str());
        return false;
    }
    vkb::Swapchain built = result.value();
    output.swapchain = built.swapchain;
    output.format = built.image_format;
    output.color_space = built.color_space;
    output.extent = built.extent;
    auto images = built.get_images();
    if (!images) {
        std::fprintf(stderr, "[VkSwapchain] Cannot enumerate images: %s\n",
                     images.error().message().c_str());
        return false;
    }
    output.images = std::move(images.value());
    return !output.images.empty();
}

bool VkSwapchain::create_image_views(VkContext& ctx, OwnedState& output) {
    output.image_views.resize(output.images.size(), VK_NULL_HANDLE);
    for (std::size_t index = 0; index < output.images.size(); ++index) {
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = output.images[index];
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = output.format;
        info.components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY};
        info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (!ayther::runtime::vulkan::require_vk_success(
                "vkCreateImageView [VkSwapchain]",
                ctx.calls().create_image_view(ctx.device(), &info, nullptr,
                                              &output.image_views[index]))) {
            return false;
        }
    }
    return true;
}

bool VkSwapchain::create_sync(VkContext& ctx, OwnedState& output) {
    for (SwapFrame& frame : output.frames) {
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = ctx.graphics_family();
        if (!ayther::runtime::vulkan::require_vk_success(
                "vkCreateCommandPool", ctx.calls().create_command_pool(
                    ctx.device(), &pool_info, nullptr, &frame.cmd_pool))) {
            return false;
        }

        VkCommandBufferAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate_info.commandPool = frame.cmd_pool;
        allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate_info.commandBufferCount = 1;
        if (!ayther::runtime::vulkan::require_vk_success(
                "vkAllocateCommandBuffers",
                ctx.calls().allocate_command_buffers(
                    ctx.device(), &allocate_info, &frame.cmd))) {
            return false;
        }

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (!ayther::runtime::vulkan::require_vk_success(
                "vkCreateFence", ctx.calls().create_fence(
                    ctx.device(), &fence_info, nullptr, &frame.fence))) {
            return false;
        }

        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (!ayther::runtime::vulkan::require_vk_success(
                "vkCreateSemaphore [image-ready]",
                ctx.calls().create_semaphore(ctx.device(), &semaphore_info,
                                             nullptr, &frame.image_ready)) ||
            !ayther::runtime::vulkan::require_vk_success(
                "vkCreateSemaphore [render-done]",
                ctx.calls().create_semaphore(ctx.device(), &semaphore_info,
                                             nullptr, &frame.render_done))) {
            return false;
        }
    }
    return true;
}

void VkSwapchain::destroy_owned(
    const VkDevice device,
    const ayther::runtime::vulkan::VulkanCalls& calls,
    OwnedState& state) noexcept {
    if (device == VK_NULL_HANDLE) {
        state = {};
        return;
    }
    for (SwapFrame& frame : state.frames) {
        if (frame.render_done != VK_NULL_HANDLE) {
            calls.destroy_semaphore(device, frame.render_done, nullptr);
        }
        if (frame.image_ready != VK_NULL_HANDLE) {
            calls.destroy_semaphore(device, frame.image_ready, nullptr);
        }
        if (frame.fence != VK_NULL_HANDLE) {
            calls.destroy_fence(device, frame.fence, nullptr);
        }
        if (frame.cmd_pool != VK_NULL_HANDLE) {
            calls.destroy_command_pool(device, frame.cmd_pool, nullptr);
        }
        frame = {};
    }
    for (const VkImageView image_view : state.image_views) {
        if (image_view != VK_NULL_HANDLE) {
            calls.destroy_image_view(device, image_view, nullptr);
        }
    }
    state.image_views.clear();
    state.images.clear();
    if (state.swapchain != VK_NULL_HANDLE) {
        calls.destroy_swapchain(device, state.swapchain, nullptr);
    }
    state = {};
}

bool VkSwapchain::accepts(const AcquiredFrame& frame) const noexcept {
    return frame.owner_ == this && frame.generation_ == generation_ &&
           frame.serial_ != 0 && frame.serial_ == active_serial_ &&
           frame.frame_slot_ < state_.frames.size() &&
           frame.image_index_ < state_.images.size() &&
           frame.image_index_ < state_.image_views.size();
}
