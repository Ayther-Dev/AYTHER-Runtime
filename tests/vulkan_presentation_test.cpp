#include "player_overlay.h"
#include "vulkan_backend/vk_context.h"
#include "vulkan_backend/vk_postprocess.h"
#include "vulkan_backend/vk_present.h"
#include "vulkan_backend/vk_swapchain.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<ayther::PlayerOverlay>);
static_assert(!std::is_copy_constructible_v<VkContext>);
static_assert(!std::is_copy_constructible_v<VkPostProcess>);
static_assert(!std::is_copy_constructible_v<VkSwapchain>);

int main() {
    // Compiling and linking the real Runtime presentation implementation is the
    // contract. Hardware behavior is covered by vulkan_context_tests --gpu and
    // Engine's renderer GPU smokes.
    return 0;
}
