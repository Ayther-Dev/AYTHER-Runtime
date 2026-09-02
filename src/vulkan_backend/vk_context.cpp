#include "vulkan_backend/vk_context.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <cstdio>
#include <string>

namespace {

#ifdef NDEBUG
constexpr bool kEnableValidation = false;
#else
constexpr bool kEnableValidation = true;
#endif

void log_failure(const char* operation, const char* detail) {
    std::fprintf(stderr, "[runtime.vulkan] %s: %s\n", operation,
                 detail != nullptr ? detail : "unknown error");
}

}  // namespace

VkContext::~VkContext() {
    shutdown();
}

bool VkContext::init(SDL_Window* window) {
    if (window == nullptr) {
        log_failure("initialization failed", "null SDL window");
        return false;
    }
    if (is_ready()) {
        return true;
    }
    shutdown();

    vkb::InstanceBuilder instance_builder;
    instance_builder.set_app_name("Ayther Runtime")
        .set_engine_name("Ayther")
        .require_api_version(1, 1, 0);
    if (kEnableValidation) {
        instance_builder.request_validation_layers(true)
            .use_default_debug_messenger();
    }

    auto instance_result = instance_builder.build();
    if (!instance_result) {
        log_failure("instance creation failed",
                    instance_result.error().message().c_str());
        return false;
    }

    const vkb::Instance bootstrap_instance = instance_result.value();
    engine_view_.instance_handle = bootstrap_instance.instance;
    debug_messenger_ = bootstrap_instance.debug_messenger;

    if (!SDL_Vulkan_CreateSurface(window, instance(), nullptr, &surface_)) {
        log_failure("surface creation failed", SDL_GetError());
        shutdown();
        return false;
    }

    vkb::PhysicalDeviceSelector device_selector(bootstrap_instance);
    auto physical_device_result = device_selector.set_surface(surface_)
        .set_minimum_version(1, 1)
        .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
        .select();
    if (!physical_device_result) {
        log_failure("physical-device selection failed",
                    physical_device_result.error().message().c_str());
        shutdown();
        return false;
    }

    const vkb::PhysicalDevice bootstrap_physical_device =
        physical_device_result.value();
    engine_view_.physical_device_handle =
        bootstrap_physical_device.physical_device;
    gpu_name_ = bootstrap_physical_device.properties.deviceName;

    vkb::DeviceBuilder device_builder(bootstrap_physical_device);
    auto device_result = device_builder.build();
    if (!device_result) {
        log_failure("logical-device creation failed",
                    device_result.error().message().c_str());
        shutdown();
        return false;
    }

    const vkb::Device bootstrap_device = device_result.value();
    engine_view_.device_handle = bootstrap_device.device;

    const auto graphics_queue =
        bootstrap_device.get_queue(vkb::QueueType::graphics);
    const auto present_queue =
        bootstrap_device.get_queue(vkb::QueueType::present);
    const auto graphics_family =
        bootstrap_device.get_queue_index(vkb::QueueType::graphics);
    const auto present_family =
        bootstrap_device.get_queue_index(vkb::QueueType::present);
    if (!graphics_queue || !present_queue || !graphics_family ||
        !present_family) {
        log_failure("queue retrieval failed", "required queue unavailable");
        shutdown();
        return false;
    }

    engine_view_.graphics_queue_handle = graphics_queue.value();
    present_queue_ = present_queue.value();
    engine_view_.graphics_queue_family_index = graphics_family.value();
    present_family_ = present_family.value();

    VmaAllocatorCreateInfo allocator_info{};
    allocator_info.physicalDevice = physical_device();
    allocator_info.device = device();
    allocator_info.instance = instance();
    allocator_info.vulkanApiVersion = VK_API_VERSION_1_1;
    if (vmaCreateAllocator(&allocator_info,
                           &engine_view_.allocator_handle) != VK_SUCCESS) {
        log_failure("VMA allocator creation failed", "vmaCreateAllocator");
        shutdown();
        return false;
    }

    const auto& properties = bootstrap_physical_device.properties;
    std::fprintf(stdout,
        "[runtime.vulkan] GPU: %s, graphics queue %u, present queue %u, "
        "validation %s\n",
        gpu_name_.c_str(), engine_view_.graphics_queue_family_index,
        present_family_,
        kEnableValidation ? "on" : "off");
    std::fprintf(stdout,
        "[runtime.vulkan] vendor=0x%04X driver=%u.%u.%u api=%u.%u.%u\n",
        static_cast<unsigned>(properties.vendorID),
        VK_VERSION_MAJOR(properties.driverVersion),
        VK_VERSION_MINOR(properties.driverVersion),
        VK_VERSION_PATCH(properties.driverVersion),
        VK_VERSION_MAJOR(properties.apiVersion),
        VK_VERSION_MINOR(properties.apiVersion),
        VK_VERSION_PATCH(properties.apiVersion));
    return true;
}

void VkContext::shutdown() noexcept {
    if (device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device());
    }

    if (engine_view_.allocator_handle != nullptr) {
        vmaDestroyAllocator(engine_view_.allocator_handle);
        engine_view_.allocator_handle = nullptr;
    }

    if (device() != VK_NULL_HANDLE) {
        vkDestroyDevice(device(), nullptr);
        engine_view_.device_handle = VK_NULL_HANDLE;
    }
    engine_view_.graphics_queue_handle = VK_NULL_HANDLE;
    engine_view_.graphics_queue_family_index = VK_QUEUE_FAMILY_IGNORED;
    present_queue_ = VK_NULL_HANDLE;
    present_family_ = VK_QUEUE_FAMILY_IGNORED;

    if (surface_ != VK_NULL_HANDLE && instance() != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance(), surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }

    if (debug_messenger_ != VK_NULL_HANDLE &&
        instance() != VK_NULL_HANDLE) {
        const auto destroy_debug_messenger =
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance(),
                                      "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy_debug_messenger != nullptr) {
            destroy_debug_messenger(instance(), debug_messenger_, nullptr);
        }
        debug_messenger_ = VK_NULL_HANDLE;
    }

    if (instance() != VK_NULL_HANDLE) {
        vkDestroyInstance(instance(), nullptr);
        engine_view_.instance_handle = VK_NULL_HANDLE;
    }
    engine_view_.physical_device_handle = VK_NULL_HANDLE;
    gpu_name_.clear();
}
