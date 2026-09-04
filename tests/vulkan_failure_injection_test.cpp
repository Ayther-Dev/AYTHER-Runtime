#include "vulkan_backend/vk_context.h"
#include "vulkan_backend/vk_postprocess.h"
#include "vulkan_backend/vk_swapchain.h"

#include <cstdint>
#include <type_traits>

#ifndef AYTHER_TEST_VERT_SPV
#error AYTHER_TEST_VERT_SPV is required
#endif
#ifndef AYTHER_TEST_FRAG_SPV
#error AYTHER_TEST_FRAG_SPV is required
#endif

namespace {

template <typename Handle>
Handle handle(const std::uintptr_t value) {
    if constexpr (std::is_pointer_v<Handle>) {
        return reinterpret_cast<Handle>(value);
    } else {
        return static_cast<Handle>(value);
    }
}

int injection_step = 0;
int fail_at_step = 0;
int live_objects = 0;
std::uintptr_t next_handle = 100;
VkResult device_wait_result = VK_SUCCESS;

bool injected_failure() {
    ++injection_step;
    return fail_at_step != 0 && injection_step == fail_at_step;
}

template <typename Handle>
VkResult create_handle(Handle* output) {
    if (injected_failure()) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    *output = handle<Handle>(next_handle++);
    ++live_objects;
    return VK_SUCCESS;
}

void destroy_handle() {
    --live_objects;
}

VKAPI_ATTR VkResult VKAPI_CALL fake_create_render_pass(
    VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*,
    VkRenderPass* output) {
    return create_handle(output);
}

VKAPI_ATTR VkResult VKAPI_CALL fake_create_shader_module(
    VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*,
    VkShaderModule* output) {
    return create_handle(output);
}

VKAPI_ATTR VkResult VKAPI_CALL fake_create_descriptor_set_layout(
    VkDevice, const VkDescriptorSetLayoutCreateInfo*,
    const VkAllocationCallbacks*, VkDescriptorSetLayout* output) {
    return create_handle(output);
}

VKAPI_ATTR VkResult VKAPI_CALL fake_create_pipeline_layout(
    VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*,
    VkPipelineLayout* output) {
    return create_handle(output);
}

VKAPI_ATTR VkResult VKAPI_CALL fake_create_graphics_pipelines(
    VkDevice, VkPipelineCache, std::uint32_t,
    const VkGraphicsPipelineCreateInfo*, const VkAllocationCallbacks*,
    VkPipeline* output) {
    return create_handle(output);
}

VKAPI_ATTR VkResult VKAPI_CALL fake_create_sampler(
    VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*,
    VkSampler* output) {
    return create_handle(output);
}

VKAPI_ATTR VkResult VKAPI_CALL fake_create_descriptor_pool(
    VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*,
    VkDescriptorPool* output) {
    return create_handle(output);
}

VKAPI_ATTR VkResult VKAPI_CALL fake_allocate_descriptor_sets(
    VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* output) {
    if (injected_failure()) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    for (std::uint32_t i = 0; i < info->descriptorSetCount; ++i) {
        output[i] = handle<VkDescriptorSet>(next_handle++);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fake_create_framebuffer(
    VkDevice, const VkFramebufferCreateInfo*, const VkAllocationCallbacks*,
    VkFramebuffer* output) {
    return create_handle(output);
}

VKAPI_ATTR VkResult VKAPI_CALL fake_create_command_pool(
    VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*,
    VkCommandPool* output) {
    return create_handle(output);
}

VKAPI_ATTR VkResult VKAPI_CALL fake_allocate_command_buffers(
    VkDevice, const VkCommandBufferAllocateInfo* info,
    VkCommandBuffer* output) {
    if (injected_failure()) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    for (std::uint32_t i = 0; i < info->commandBufferCount; ++i) {
        output[i] = handle<VkCommandBuffer>(next_handle++);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL fake_create_fence(
    VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*,
    VkFence* output) {
    return create_handle(output);
}

VKAPI_ATTR VkResult VKAPI_CALL fake_create_semaphore(
    VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*,
    VkSemaphore* output) {
    return create_handle(output);
}

VKAPI_ATTR VkResult VKAPI_CALL fake_create_image_view(
    VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*,
    VkImageView* output) {
    return create_handle(output);
}

VKAPI_ATTR VkResult VKAPI_CALL fake_device_wait_idle(VkDevice) {
    return device_wait_result;
}

#define AYTHER_FAKE_DESTROY(name, type)                                      \
    VKAPI_ATTR void VKAPI_CALL name(                                         \
        VkDevice, type object, const VkAllocationCallbacks*) {               \
        if (object != VK_NULL_HANDLE) destroy_handle();                      \
    }

AYTHER_FAKE_DESTROY(fake_destroy_render_pass, VkRenderPass)
AYTHER_FAKE_DESTROY(fake_destroy_shader_module, VkShaderModule)
AYTHER_FAKE_DESTROY(fake_destroy_descriptor_set_layout, VkDescriptorSetLayout)
AYTHER_FAKE_DESTROY(fake_destroy_pipeline_layout, VkPipelineLayout)
AYTHER_FAKE_DESTROY(fake_destroy_pipeline, VkPipeline)
AYTHER_FAKE_DESTROY(fake_destroy_sampler, VkSampler)
AYTHER_FAKE_DESTROY(fake_destroy_descriptor_pool, VkDescriptorPool)
AYTHER_FAKE_DESTROY(fake_destroy_framebuffer, VkFramebuffer)
AYTHER_FAKE_DESTROY(fake_destroy_command_pool, VkCommandPool)
AYTHER_FAKE_DESTROY(fake_destroy_fence, VkFence)
AYTHER_FAKE_DESTROY(fake_destroy_semaphore, VkSemaphore)
AYTHER_FAKE_DESTROY(fake_destroy_image_view, VkImageView)

#undef AYTHER_FAKE_DESTROY

VKAPI_ATTR void VKAPI_CALL fake_destroy_device(
    VkDevice, const VkAllocationCallbacks*) {}
VKAPI_ATTR void VKAPI_CALL fake_destroy_instance(
    VkInstance, const VkAllocationCallbacks*) {}
VKAPI_ATTR void VKAPI_CALL fake_destroy_surface(
    VkInstance, VkSurfaceKHR, const VkAllocationCallbacks*) {}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fake_get_instance_proc_addr(
    VkInstance, const char*) {
    return nullptr;
}
void fake_destroy_allocator(VmaAllocator) {}

ayther::runtime::vulkan::VulkanCalls injected_calls() {
    auto calls = ayther::runtime::vulkan::default_vulkan_calls();
    calls.create_render_pass = &fake_create_render_pass;
    calls.create_shader_module = &fake_create_shader_module;
    calls.create_descriptor_set_layout = &fake_create_descriptor_set_layout;
    calls.create_pipeline_layout = &fake_create_pipeline_layout;
    calls.create_graphics_pipelines = &fake_create_graphics_pipelines;
    calls.create_sampler = &fake_create_sampler;
    calls.create_descriptor_pool = &fake_create_descriptor_pool;
    calls.allocate_descriptor_sets = &fake_allocate_descriptor_sets;
    calls.create_framebuffer = &fake_create_framebuffer;
    calls.create_command_pool = &fake_create_command_pool;
    calls.allocate_command_buffers = &fake_allocate_command_buffers;
    calls.create_fence = &fake_create_fence;
    calls.create_semaphore = &fake_create_semaphore;
    calls.create_image_view = &fake_create_image_view;
    calls.device_wait_idle = &fake_device_wait_idle;
    calls.destroy_render_pass = &fake_destroy_render_pass;
    calls.destroy_shader_module = &fake_destroy_shader_module;
    calls.destroy_descriptor_set_layout = &fake_destroy_descriptor_set_layout;
    calls.destroy_pipeline_layout = &fake_destroy_pipeline_layout;
    calls.destroy_pipeline = &fake_destroy_pipeline;
    calls.destroy_sampler = &fake_destroy_sampler;
    calls.destroy_descriptor_pool = &fake_destroy_descriptor_pool;
    calls.destroy_framebuffer = &fake_destroy_framebuffer;
    calls.destroy_command_pool = &fake_destroy_command_pool;
    calls.destroy_fence = &fake_destroy_fence;
    calls.destroy_semaphore = &fake_destroy_semaphore;
    calls.destroy_image_view = &fake_destroy_image_view;
    calls.destroy_allocator = &fake_destroy_allocator;
    calls.destroy_device = &fake_destroy_device;
    calls.destroy_instance = &fake_destroy_instance;
    calls.destroy_surface = &fake_destroy_surface;
    calls.get_instance_proc_addr = &fake_get_instance_proc_addr;
    return calls;
}

void reset_injection(const int fail_at) {
    injection_step = 0;
    fail_at_step = fail_at;
    live_objects = 0;
}

}  // namespace

struct VkContextTestAccess {
    static void seed(VkContext& context) {
        context.engine_view_.instance_handle = handle<VkInstance>(1);
        context.engine_view_.physical_device_handle = handle<VkPhysicalDevice>(2);
        context.engine_view_.device_handle = handle<VkDevice>(3);
        context.engine_view_.graphics_queue_handle = handle<VkQueue>(4);
        context.engine_view_.graphics_queue_family_index = 0;
        context.engine_view_.allocator_handle = handle<VmaAllocator>(5);
        context.surface_ = handle<VkSurfaceKHR>(6);
        context.present_queue_ = handle<VkQueue>(7);
        context.present_family_ = 0;
    }
};

struct VkSwapchainTestAccess {
    static void seed(
        VkSwapchain& swap,
        const ayther::runtime::vulkan::VulkanCalls& calls) {
        swap.device_ = handle<VkDevice>(3);
        swap.calls_ = calls;
        swap.state_.swapchain = handle<VkSwapchainKHR>(8);
        swap.state_.format = VK_FORMAT_B8G8R8A8_UNORM;
        swap.state_.extent = {640, 480};
        swap.state_.images = {handle<VkImage>(9), handle<VkImage>(10)};
        swap.state_.image_views = {
            handle<VkImageView>(11), handle<VkImageView>(12)};
    }

    static void disarm(VkSwapchain& swap) {
        swap.device_ = VK_NULL_HANDLE;
        swap.state_ = {};
    }

    static bool create_and_destroy_sync(VkSwapchain& swap, VkContext& context,
                                        const ayther::runtime::vulkan::VulkanCalls& calls) {
        VkSwapchain::OwnedState pending;
        const bool created = swap.create_sync(context, pending);
        VkSwapchain::destroy_owned(handle<VkDevice>(3), calls, pending);
        return created;
    }

    static bool create_and_destroy_views(VkSwapchain& swap, VkContext& context,
                                         const ayther::runtime::vulkan::VulkanCalls& calls) {
        VkSwapchain::OwnedState pending;
        pending.format = VK_FORMAT_B8G8R8A8_UNORM;
        pending.images = {handle<VkImage>(70), handle<VkImage>(71)};
        const bool created = swap.create_image_views(context, pending);
        VkSwapchain::destroy_owned(handle<VkDevice>(3), calls, pending);
        return created;
    }
};

int main() {
    const auto calls = injected_calls();
    VkContext context{calls};
    VkContextTestAccess::seed(context);
    VkSwapchain swap;
    VkSwapchainTestAccess::seed(swap, calls);

    constexpr int sync_creation_points = 10;
    for (int point = 1; point <= sync_creation_points; ++point) {
        reset_injection(point);
        if (VkSwapchainTestAccess::create_and_destroy_sync(
                swap, context, calls) || live_objects != 0) {
            VkSwapchainTestAccess::disarm(swap);
            return 50 + point;
        }
    }
    for (int point = 1; point <= 2; ++point) {
        reset_injection(point);
        if (VkSwapchainTestAccess::create_and_destroy_views(
                swap, context, calls) || live_objects != 0) {
            VkSwapchainTestAccess::disarm(swap);
            return 70 + point;
        }
    }

    // Every creation/allocation boundary must roll back all earlier objects.
    constexpr int creation_points = 12;
    for (int point = 1; point <= creation_points; ++point) {
        reset_injection(point);
        VkPostProcess postprocess;
        if (postprocess.init(context, swap, AYTHER_TEST_VERT_SPV,
                             AYTHER_TEST_FRAG_SPV) ||
            postprocess.is_ready() || live_objects != 0) {
            VkSwapchainTestAccess::disarm(swap);
            return point;
        }
        postprocess.shutdown(context);
        if (live_objects != 0) {
            VkSwapchainTestAccess::disarm(swap);
            return 20 + point;
        }
    }

    reset_injection(0);
    VkPostProcess complete;
    if (!complete.init(context, swap, AYTHER_TEST_VERT_SPV,
                       AYTHER_TEST_FRAG_SPV) ||
        !complete.is_ready() || injection_step != creation_points) {
        VkSwapchainTestAccess::disarm(swap);
        return 40;
    }
    complete.shutdown(context);
    complete.shutdown(context);
    if (live_objects != 0 || complete.is_ready()) {
        VkSwapchainTestAccess::disarm(swap);
        return 41;
    }

    VkSwapchainTestAccess::disarm(swap);
    device_wait_result = VK_ERROR_DEVICE_LOST;
    context.shutdown();
    context.shutdown();
    return context.is_ready() ? 42 : 0;
}
