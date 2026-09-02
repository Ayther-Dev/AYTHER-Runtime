#pragma once

#include <ayther/engine/vulkan_interop.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

struct SDL_Window;

/// Runtime-owned Vulkan context.
///
/// Runtime creates and destroys the instance, window surface, device, queues,
/// and VMA allocator. Engine receives only engine_view(), whose handles are
/// borrowed and exclude all presentation state.
class VkContext final {
public:
    VkContext() = default;
    ~VkContext();

    VkContext(const VkContext&) = delete;
    VkContext& operator=(const VkContext&) = delete;
    VkContext(VkContext&&) = delete;
    VkContext& operator=(VkContext&&) = delete;

    /// Initialize from an SDL3 window created with SDL_WINDOW_VULKAN.
    [[nodiscard]] bool init(SDL_Window* window);

    /// Wait for the device and destroy all owned objects in reverse order.
    /// Safe after partial initialization and safe to call more than once.
    void shutdown() noexcept;

    [[nodiscard]] bool is_ready() const noexcept {
        return engine_view_.is_valid() && surface_ != VK_NULL_HANDLE &&
               present_queue_ != VK_NULL_HANDLE &&
               present_family_ != VK_QUEUE_FAMILY_IGNORED;
    }

    [[nodiscard]] VkInstance instance() const noexcept {
        return engine_view_.instance();
    }
    [[nodiscard]] VkSurfaceKHR surface() const noexcept { return surface_; }
    [[nodiscard]] VkPhysicalDevice physical_device() const noexcept {
        return engine_view_.physical_device();
    }
    [[nodiscard]] VkDevice device() const noexcept {
        return engine_view_.device();
    }
    [[nodiscard]] VkQueue graphics_queue() const noexcept {
        return engine_view_.graphics_queue();
    }
    [[nodiscard]] VkQueue present_queue() const noexcept {
        return present_queue_;
    }
    [[nodiscard]] std::uint32_t graphics_family() const noexcept {
        return engine_view_.graphics_family();
    }
    [[nodiscard]] std::uint32_t present_family() const noexcept {
        return present_family_;
    }
    [[nodiscard]] VmaAllocator_T* allocator() const noexcept {
        return engine_view_.allocator();
    }
    [[nodiscard]] const std::string& gpu_name() const noexcept {
        return gpu_name_;
    }

    /// Public Engine interop value. Its handles remain owned by this context.
    [[nodiscard]] ayther::engine::VulkanContextView& engine_view() noexcept {
        return engine_view_;
    }
    [[nodiscard]] const ayther::engine::VulkanContextView&
    engine_view() const noexcept {
        return engine_view_;
    }

private:
    ayther::engine::VulkanContextView engine_view_{};
    VkDebugUtilsMessengerEXT debug_messenger_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkQueue present_queue_{VK_NULL_HANDLE};
    std::uint32_t present_family_{VK_QUEUE_FAMILY_IGNORED};
    std::string gpu_name_;
};
