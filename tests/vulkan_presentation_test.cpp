#include "player_overlay.h"
#include "vulkan_backend/vk_context.h"
#include "vulkan_backend/vk_postprocess.h"
#include "vulkan_backend/vk_present.h"
#include "vulkan_backend/vk_swapchain.h"

#include <cstdint>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<ayther::PlayerOverlay>);
static_assert(!std::is_copy_constructible_v<VkContext>);
static_assert(!std::is_copy_constructible_v<VkPostProcess>);
static_assert(!std::is_copy_constructible_v<VkSwapchain>);
static_assert(!std::is_copy_constructible_v<AcquiredFrame>);
static_assert(std::is_move_constructible_v<AcquiredFrame>);

namespace {

template <typename Handle>
Handle handle(const std::uintptr_t value) {
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<Handle>(value);
    } else {
        return static_cast<Handle>(value);
    }
}

VkResult acquire_result = VK_SUCCESS;
VkResult present_result = VK_SUCCESS;
VkResult wait_result = VK_SUCCESS;
VkResult reset_pool_result = VK_SUCCESS;
VkResult begin_result = VK_SUCCESS;
VkResult end_result = VK_SUCCESS;
VkResult reset_fence_result = VK_SUCCESS;
VkResult submit_result = VK_SUCCESS;
std::uint32_t acquired_index = 0;
std::uint32_t presented_index = UINT32_MAX;
int present_calls = 0;
int destroyed_pools = 0;
int destroyed_fences = 0;
int destroyed_semaphores = 0;
int destroyed_views = 0;
int destroyed_swapchains = 0;

VKAPI_ATTR VkResult VKAPI_CALL fake_wait_for_fences(
    VkDevice, std::uint32_t, const VkFence*, VkBool32, std::uint64_t) {
    return wait_result;
}

VKAPI_ATTR VkResult VKAPI_CALL fake_acquire_next_image(
    VkDevice, VkSwapchainKHR, std::uint64_t, VkSemaphore, VkFence,
    std::uint32_t* output) {
    *output = acquired_index;
    return acquire_result;
}

VKAPI_ATTR VkResult VKAPI_CALL fake_reset_command_pool(
    VkDevice, VkCommandPool, VkCommandPoolResetFlags) {
    return reset_pool_result;
}

VKAPI_ATTR VkResult VKAPI_CALL fake_begin_command_buffer(
    VkCommandBuffer, const VkCommandBufferBeginInfo*) {
    return begin_result;
}

VKAPI_ATTR VkResult VKAPI_CALL fake_end_command_buffer(VkCommandBuffer) {
    return end_result;
}

VKAPI_ATTR VkResult VKAPI_CALL fake_reset_fences(
    VkDevice, std::uint32_t, const VkFence*) {
    return reset_fence_result;
}

VKAPI_ATTR VkResult VKAPI_CALL fake_queue_submit(
    VkQueue, std::uint32_t, const VkSubmitInfo*, VkFence) {
    return submit_result;
}

VKAPI_ATTR VkResult VKAPI_CALL fake_queue_present(
    VkQueue, const VkPresentInfoKHR* info) {
    ++present_calls;
    presented_index = *info->pImageIndices;
    return present_result;
}

VKAPI_ATTR VkResult VKAPI_CALL fake_device_wait_idle(VkDevice) {
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL fake_destroy_command_pool(
    VkDevice, VkCommandPool, const VkAllocationCallbacks*) {
    ++destroyed_pools;
}

VKAPI_ATTR void VKAPI_CALL fake_destroy_fence(
    VkDevice, VkFence, const VkAllocationCallbacks*) {
    ++destroyed_fences;
}

VKAPI_ATTR void VKAPI_CALL fake_destroy_semaphore(
    VkDevice, VkSemaphore, const VkAllocationCallbacks*) {
    ++destroyed_semaphores;
}

VKAPI_ATTR void VKAPI_CALL fake_destroy_image_view(
    VkDevice, VkImageView, const VkAllocationCallbacks*) {
    ++destroyed_views;
}

VKAPI_ATTR void VKAPI_CALL fake_destroy_swapchain(
    VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*) {
    ++destroyed_swapchains;
}

ayther::runtime::vulkan::VulkanCalls frame_calls() {
    auto calls = ayther::runtime::vulkan::default_vulkan_calls();
    calls.wait_for_fences = &fake_wait_for_fences;
    calls.acquire_next_image = &fake_acquire_next_image;
    calls.reset_command_pool = &fake_reset_command_pool;
    calls.begin_command_buffer = &fake_begin_command_buffer;
    calls.end_command_buffer = &fake_end_command_buffer;
    calls.reset_fences = &fake_reset_fences;
    calls.queue_submit = &fake_queue_submit;
    calls.queue_present = &fake_queue_present;
    calls.device_wait_idle = &fake_device_wait_idle;
    calls.destroy_command_pool = &fake_destroy_command_pool;
    calls.destroy_fence = &fake_destroy_fence;
    calls.destroy_semaphore = &fake_destroy_semaphore;
    calls.destroy_image_view = &fake_destroy_image_view;
    calls.destroy_swapchain = &fake_destroy_swapchain;
    return calls;
}

}  // namespace

struct VkSwapchainTestAccess {
    static void seed(
        VkSwapchain& swap,
        const ayther::runtime::vulkan::VulkanCalls& calls) {
        swap.device_ = handle<VkDevice>(1);
        swap.calls_ = calls;
        swap.state_.swapchain = handle<VkSwapchainKHR>(2);
        swap.state_.format = VK_FORMAT_B8G8R8A8_UNORM;
        swap.state_.extent = {640, 480};
        swap.state_.images = {handle<VkImage>(3), handle<VkImage>(4)};
        swap.state_.image_views = {
            handle<VkImageView>(5), handle<VkImageView>(6)};
        for (std::uint32_t i = 0; i < VkSwapchain::kMaxFrames; ++i) {
            auto& frame = swap.state_.frames[i];
            frame.cmd_pool = handle<VkCommandPool>(10 + i);
            frame.cmd = handle<VkCommandBuffer>(20 + i);
            frame.fence = handle<VkFence>(30 + i);
            frame.image_ready = handle<VkSemaphore>(40 + i);
            frame.render_done = handle<VkSemaphore>(50 + i);
        }
    }

    static void invalidate_for_rebuild(VkSwapchain& swap) {
        swap.active_serial_ = 0;
        ++swap.generation_;
    }
};

int main() {
    const auto calls = frame_calls();
    VkContext context{calls};
    VkSwapchain swap;
    VkSwapchainTestAccess::seed(swap, calls);

    AcquiredFrame absent;
    if (absent.valid() || swap.end_frame(context, absent)) {
        return 1;
    }

    wait_result = VK_ERROR_DEVICE_LOST;
    if (swap.begin_frame(context).has_value()) {
        return 2;
    }
    wait_result = VK_SUCCESS;

    acquire_result = VK_ERROR_OUT_OF_DATE_KHR;
    if (swap.begin_frame(context).has_value()) {
        return 3;
    }

    acquire_result = VK_SUCCESS;
    acquired_index = 99;
    if (swap.begin_frame(context).has_value()) {
        return 4;
    }

    acquired_index = 0;
    reset_pool_result = VK_ERROR_DEVICE_LOST;
    if (swap.begin_frame(context).has_value()) {
        return 5;
    }
    reset_pool_result = VK_SUCCESS;

    begin_result = VK_ERROR_DEVICE_LOST;
    if (swap.begin_frame(context).has_value()) {
        return 6;
    }
    begin_result = VK_SUCCESS;

    acquired_index = 1;
    auto acquired = swap.begin_frame(context);
    const VkFramebuffer short_framebuffers[] = {handle<VkFramebuffer>(60)};
    const VkFramebuffer complete_framebuffers[] = {
        handle<VkFramebuffer>(60), handle<VkFramebuffer>(61)};
    if (!acquired || !acquired->valid() || acquired->image_index() != 1 ||
        acquired->image() != handle<VkImage>(4) ||
        acquired->image_view() != handle<VkImageView>(6) ||
        acquired->extent().width != 640 ||
        acquired->framebuffer(short_framebuffers).has_value() ||
        acquired->framebuffer(complete_framebuffers) != complete_framebuffers[1] ||
        swap.begin_frame(context).has_value()) {
        return 7;
    }

    if (!swap.end_frame(context, *acquired) || acquired->valid() ||
        present_calls != 1 || presented_index != 1 ||
        swap.end_frame(context, *acquired)) {
        return 8;
    }

    const auto expect_end_failure = [&](VkResult& injected_result) {
        acquired_index = 0;
        auto frame = swap.begin_frame(context);
        if (!frame || !frame->valid()) {
            return false;
        }
        injected_result = VK_ERROR_DEVICE_LOST;
        const bool unexpectedly_succeeded = swap.end_frame(context, *frame);
        injected_result = VK_SUCCESS;
        return !unexpectedly_succeeded && !frame->valid();
    };
    if (!expect_end_failure(end_result)) {
        return 9;
    }
    if (!expect_end_failure(reset_fence_result)) {
        return 10;
    }
    if (!expect_end_failure(submit_result)) {
        return 11;
    }
    if (!expect_end_failure(present_result)) {
        return 12;
    }

    acquired_index = 0;
    auto stale = swap.begin_frame(context);
    if (!stale || !stale->valid()) {
        return 13;
    }
    VkSwapchainTestAccess::invalidate_for_rebuild(swap);
    if (stale->valid() || swap.end_frame(context, *stale)) {
        return 14;
    }

    swap.shutdown();
    swap.shutdown();
    if (swap.is_ready() || destroyed_pools != 2 || destroyed_fences != 2 ||
        destroyed_semaphores != 4 || destroyed_views != 2 ||
        destroyed_swapchains != 1) {
        return 15;
    }
    return 0;
}
