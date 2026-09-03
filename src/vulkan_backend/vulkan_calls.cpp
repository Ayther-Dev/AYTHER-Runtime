#include "vulkan_backend/vulkan_calls.h"

namespace ayther::runtime::vulkan {

const VulkanCalls& default_vulkan_calls() noexcept {
    static const VulkanCalls calls = [] {
        VulkanCalls value;
        value.create_allocator = &vmaCreateAllocator;
        value.destroy_allocator = &vmaDestroyAllocator;

        value.acquire_next_image = &vkAcquireNextImageKHR;
        value.allocate_command_buffers = &vkAllocateCommandBuffers;
        value.allocate_descriptor_sets = &vkAllocateDescriptorSets;
        value.begin_command_buffer = &vkBeginCommandBuffer;
        value.create_command_pool = &vkCreateCommandPool;
        value.create_descriptor_pool = &vkCreateDescriptorPool;
        value.create_descriptor_set_layout = &vkCreateDescriptorSetLayout;
        value.create_fence = &vkCreateFence;
        value.create_framebuffer = &vkCreateFramebuffer;
        value.create_graphics_pipelines = &vkCreateGraphicsPipelines;
        value.create_image_view = &vkCreateImageView;
        value.create_pipeline_layout = &vkCreatePipelineLayout;
        value.create_render_pass = &vkCreateRenderPass;
        value.create_sampler = &vkCreateSampler;
        value.create_semaphore = &vkCreateSemaphore;
        value.create_shader_module = &vkCreateShaderModule;
        value.device_wait_idle = &vkDeviceWaitIdle;
        value.end_command_buffer = &vkEndCommandBuffer;
        value.queue_present = &vkQueuePresentKHR;
        value.queue_submit = &vkQueueSubmit;
        value.reset_command_pool = &vkResetCommandPool;
        value.reset_fences = &vkResetFences;
        value.wait_for_fences = &vkWaitForFences;

        value.destroy_command_pool = &vkDestroyCommandPool;
        value.destroy_descriptor_pool = &vkDestroyDescriptorPool;
        value.destroy_descriptor_set_layout = &vkDestroyDescriptorSetLayout;
        value.destroy_device = &vkDestroyDevice;
        value.destroy_fence = &vkDestroyFence;
        value.destroy_framebuffer = &vkDestroyFramebuffer;
        value.destroy_image_view = &vkDestroyImageView;
        value.destroy_instance = &vkDestroyInstance;
        value.destroy_pipeline = &vkDestroyPipeline;
        value.destroy_pipeline_layout = &vkDestroyPipelineLayout;
        value.destroy_render_pass = &vkDestroyRenderPass;
        value.destroy_sampler = &vkDestroySampler;
        value.destroy_semaphore = &vkDestroySemaphore;
        value.destroy_shader_module = &vkDestroyShaderModule;
        value.destroy_surface = &vkDestroySurfaceKHR;
        value.destroy_swapchain = &vkDestroySwapchainKHR;
        value.get_instance_proc_addr = &vkGetInstanceProcAddr;
        return value;
    }();
    return calls;
}

}  // namespace ayther::runtime::vulkan
