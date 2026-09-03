#pragma once
#include "vulkan_backend/vulkan_calls.h"
// ---------------------------------------------------------------------------
// VkSwapchain — double-buffered swapchain + per-frame sync primitives.
//
// Lifecycle:
//   init(ctx, window_w, window_h)  →  acquire() → submit() → present()
//   rebuild(w, h)                  — called on SDL_EVENT_WINDOW_RESIZED
//   shutdown()
//
// Frame model: two frames in-flight.  acquire() waits for the oldest frame's
// fence, then calls vkAcquireNextImageKHR.  present() submits and queues the
// swapchain image.  No render passes here — that belongs in vk_present.
// ---------------------------------------------------------------------------
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

class VkContext;  // forward declaration

struct SwapFrame {
    VkCommandPool   cmd_pool     = VK_NULL_HANDLE;
    VkCommandBuffer cmd          = VK_NULL_HANDLE;
    VkFence         fence        = VK_NULL_HANDLE;   // GPU finished this frame
    VkSemaphore     image_ready  = VK_NULL_HANDLE;   // swapchain image acquired
    VkSemaphore     render_done  = VK_NULL_HANDLE;   // rendering finished
};

class VkSwapchain {
public:
    static constexpr uint32_t kMaxFrames = 2;   // frames in flight

    VkSwapchain()  = default;
    ~VkSwapchain() { shutdown(); }

    VkSwapchain(const VkSwapchain&)            = delete;
    VkSwapchain& operator=(const VkSwapchain&) = delete;

    // Create swapchain (BGRA8 or implementation-chosen format),
    // image views, and per-frame sync objects.
    bool init(VkContext& ctx, uint32_t width, uint32_t height);

    // Recreate after window resize / VK_ERROR_OUT_OF_DATE_KHR.
    bool rebuild(VkContext& ctx, uint32_t width, uint32_t height);

    // Destroy all resources.  Safe to call from a partially-initialized state,
    // and safe to call twice (the second one is a no-op).
    //
    // The originating device is cached so this function and the destructor can
    // release every command buffer, semaphore, fence, image view, and swapchain
    // object. This is required before device destruction (#415).
    void shutdown();

    bool is_ready() const { return swapchain_ != VK_NULL_HANDLE; }

    // ---- Per-frame interface -----------------------------------------------

    // Wait for the in-flight fence and acquire the next swapchain image.
    // Returns false on unrecoverable error (caller should shutdown).
    // Sets image_index_ and advances frame_index_.
    bool begin_frame(VkContext& ctx);

    // Submit the current frame's command buffer and present.
    // Returns false on VK_ERROR_OUT_OF_DATE_KHR (caller should rebuild).
    bool end_frame(VkContext& ctx);

    // ---- Accessors ---------------------------------------------------------
    VkSwapchainKHR         swapchain()           const { return swapchain_;   }
    VkFormat               format()              const { return format_;      }
    VkExtent2D             extent()              const { return extent_;      }
    uint32_t               image_count()         const { return static_cast<uint32_t>(images_.size()); }
    uint32_t               current_image_index() const { return image_index_; }
    const SwapFrame&       current_frame()       const { return frames_[frame_index_]; }
    SwapFrame&             current_frame()             { return frames_[frame_index_]; }
    VkImageView            image_view(uint32_t i) const { return image_views_[i]; }

    // Cached swapchain images (filled at create_swapchain time).
    // Replaces ad-hoc vkGetSwapchainImagesKHR calls every frame in
    // vk_present / vk_sprite / vk_postprocess.
    VkImage                image(uint32_t i)     const { return images_[i]; }
    VkImage                current_image()       const { return images_[image_index_]; }
    const std::vector<VkImage>& images()         const { return images_; }

private:
    bool create_swapchain(VkContext& ctx, uint32_t w, uint32_t h);
    bool create_image_views(VkContext& ctx);
    bool create_sync(VkContext& ctx);
    void destroy_swapchain_objects(VkContext& ctx);

    /// Borrowed device handle cached solely to release owned Vulkan objects.
    /// It must outlive this instance or an explicit shutdown() call.
    VkDevice                    device_       = VK_NULL_HANDLE;
    ayther::runtime::vulkan::VulkanCalls calls_{};
    VkSwapchainKHR              swapchain_    = VK_NULL_HANDLE;
    VkFormat                    format_       = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR             color_space_  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D                  extent_       = {};
    std::vector<VkImage>        images_;
    std::vector<VkImageView>    image_views_;
    SwapFrame                   frames_[kMaxFrames] = {};
    uint32_t                    frame_index_  = 0;   // cycles [0, kMaxFrames)
    uint32_t                    image_index_  = 0;   // current swapchain image
};
