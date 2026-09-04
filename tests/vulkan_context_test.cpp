#include "vulkan_backend/vk_context.h"
#include "vulkan_backend/vk_swapchain.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<VkContext>);
static_assert(!std::is_copy_assignable_v<VkContext>);
static_assert(!std::is_move_constructible_v<VkContext>);
static_assert(!std::is_move_assignable_v<VkContext>);
static_assert(std::is_same_v<
              decltype(std::declval<VkContext&>().engine_view()),
              ayther::engine::VulkanContextView&>);
static_assert(std::is_constructible_v<
              VkContext, const ayther::runtime::vulkan::VulkanCalls&>);

namespace {

VKAPI_ATTR VkResult VKAPI_CALL fake_device_wait_idle(VkDevice) {
    return VK_ERROR_DEVICE_LOST;
}

VkResult fake_create_allocator(
    const VmaAllocatorCreateInfo*, VmaAllocator* allocator) {
    *allocator = nullptr;
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

bool present_frame(VkContext& context, VkSwapchain& swapchain) {
    auto frame = swapchain.begin_frame(context);
    if (!frame) {
        return false;
    }
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = frame->image();
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(
        frame->command_buffer(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr,
        1, &barrier);
    return swapchain.end_frame(context, *frame);
}

}  // namespace

int main(int argc, char** argv) {
    auto injected_calls = ayther::runtime::vulkan::default_vulkan_calls();
    injected_calls.device_wait_idle = &fake_device_wait_idle;
    VkContext injected_context{injected_calls};
    if (injected_context.calls().device_wait_idle != &fake_device_wait_idle ||
        injected_context.wait_idle("unused-with-null-device").has_value()) {
        return 6;
    }

    VkContext context;
    if (context.is_ready() || context.engine_view().is_valid()) {
        return 1;
    }

    context.shutdown();
    context.shutdown();
    if (context.is_ready()) {
        return 2;
    }

    if (argc != 2 || std::strcmp(argv[1], "--gpu") != 0) {
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 77;
    }
    SDL_Window* window = SDL_CreateWindow(
        "AYTHER Runtime Vulkan context test", 64, 64,
        SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        SDL_Quit();
        return 77;
    }

    const bool initialized = context.init(window);
    if (!initialized) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }
    const bool contract_valid = context.is_ready() &&
        context.engine_view().is_valid() &&
        context.surface() != VK_NULL_HANDLE &&
        context.present_queue() != VK_NULL_HANDLE;

    VkSwapchain swapchain;
    bool presentation_valid = swapchain.init(context, 64, 64) &&
                              present_frame(context, swapchain);
    if (presentation_valid) {
        SDL_SetWindowSize(window, 96, 80);
        presentation_valid = swapchain.rebuild(context, 96, 80) &&
                             present_frame(context, swapchain);
    }
    swapchain.shutdown();
    swapchain.shutdown();
    const bool presentation_validation_clean =
        context.validation_error_count() == 0;
    context.shutdown();
    context.shutdown();

    auto failing_calls = ayther::runtime::vulkan::default_vulkan_calls();
    failing_calls.create_allocator = &fake_create_allocator;
    VkContext failing_context{failing_calls};
    const bool injected_failure_rolled_back =
        !failing_context.init(window) && !failing_context.is_ready();
    failing_context.shutdown();
    failing_context.shutdown();
    const bool validation_clean = presentation_validation_clean &&
                                  failing_context.validation_error_count() == 0;
    SDL_DestroyWindow(window);
    SDL_Quit();
    return contract_valid && presentation_valid &&
                   injected_failure_rolled_back && validation_clean &&
                   !swapchain.is_ready() && !context.is_ready()
               ? 0
               : 5;
}
