#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace ayther::runtime::vulkan {

/// Injectable Vulkan/VMA dispatch used by Runtime-owned initialization and
/// teardown. Production uses default_vulkan_calls(); tests may replace any
/// entry with a deterministic failure or recorder.
struct VulkanCalls final {
    decltype(&vmaCreateAllocator) create_allocator{nullptr};
    decltype(&vmaDestroyAllocator) destroy_allocator{nullptr};

    PFN_vkAcquireNextImageKHR acquire_next_image{nullptr};
    PFN_vkAllocateCommandBuffers allocate_command_buffers{nullptr};
    PFN_vkAllocateDescriptorSets allocate_descriptor_sets{nullptr};
    PFN_vkBeginCommandBuffer begin_command_buffer{nullptr};
    PFN_vkCreateCommandPool create_command_pool{nullptr};
    PFN_vkCreateDescriptorPool create_descriptor_pool{nullptr};
    PFN_vkCreateDescriptorSetLayout create_descriptor_set_layout{nullptr};
    PFN_vkCreateFence create_fence{nullptr};
    PFN_vkCreateFramebuffer create_framebuffer{nullptr};
    PFN_vkCreateGraphicsPipelines create_graphics_pipelines{nullptr};
    PFN_vkCreateImageView create_image_view{nullptr};
    PFN_vkCreatePipelineLayout create_pipeline_layout{nullptr};
    PFN_vkCreateRenderPass create_render_pass{nullptr};
    PFN_vkCreateSampler create_sampler{nullptr};
    PFN_vkCreateSemaphore create_semaphore{nullptr};
    PFN_vkCreateShaderModule create_shader_module{nullptr};
    PFN_vkDeviceWaitIdle device_wait_idle{nullptr};
    PFN_vkEndCommandBuffer end_command_buffer{nullptr};
    PFN_vkQueuePresentKHR queue_present{nullptr};
    PFN_vkQueueSubmit queue_submit{nullptr};
    PFN_vkResetCommandPool reset_command_pool{nullptr};
    PFN_vkResetFences reset_fences{nullptr};
    PFN_vkWaitForFences wait_for_fences{nullptr};

    PFN_vkDestroyCommandPool destroy_command_pool{nullptr};
    PFN_vkDestroyDescriptorPool destroy_descriptor_pool{nullptr};
    PFN_vkDestroyDescriptorSetLayout destroy_descriptor_set_layout{nullptr};
    PFN_vkDestroyDevice destroy_device{nullptr};
    PFN_vkDestroyFence destroy_fence{nullptr};
    PFN_vkDestroyFramebuffer destroy_framebuffer{nullptr};
    PFN_vkDestroyImageView destroy_image_view{nullptr};
    PFN_vkDestroyInstance destroy_instance{nullptr};
    PFN_vkDestroyPipeline destroy_pipeline{nullptr};
    PFN_vkDestroyPipelineLayout destroy_pipeline_layout{nullptr};
    PFN_vkDestroyRenderPass destroy_render_pass{nullptr};
    PFN_vkDestroySampler destroy_sampler{nullptr};
    PFN_vkDestroySemaphore destroy_semaphore{nullptr};
    PFN_vkDestroyShaderModule destroy_shader_module{nullptr};
    PFN_vkDestroySurfaceKHR destroy_surface{nullptr};
    PFN_vkDestroySwapchainKHR destroy_swapchain{nullptr};
    PFN_vkGetInstanceProcAddr get_instance_proc_addr{nullptr};
};

[[nodiscard]] const VulkanCalls& default_vulkan_calls() noexcept;

}  // namespace ayther::runtime::vulkan
