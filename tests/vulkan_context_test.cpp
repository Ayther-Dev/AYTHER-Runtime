#include "vulkan_backend/vk_context.h"

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
        return 3;
    }
    SDL_Window* window = SDL_CreateWindow(
        "AYTHER Runtime Vulkan context test", 64, 64,
        SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        SDL_Quit();
        return 4;
    }

    const bool initialized = context.init(window);
    const bool contract_valid = initialized && context.is_ready() &&
        context.engine_view().is_valid() &&
        context.surface() != VK_NULL_HANDLE &&
        context.present_queue() != VK_NULL_HANDLE;
    context.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return contract_valid && !context.is_ready() ? 0 : 5;
}
