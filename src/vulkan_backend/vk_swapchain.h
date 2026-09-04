#pragma once

#include "vulkan_backend/vulkan_calls.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class VkContext;
class VkSwapchain;

struct SwapFrame {
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore image_ready = VK_NULL_HANDLE;
    VkSemaphore render_done = VK_NULL_HANDLE;
};

/// Capability proving that one concrete swapchain image was acquired.
///
/// The token is move-only and tied to the swapchain generation that produced
/// it. Presentation consumes it; rebuild and shutdown invalidate it. Consumers
/// therefore cannot index swapchain images or synchronization arrays directly.
class AcquiredFrame final {
public:
    AcquiredFrame() noexcept = default;
    AcquiredFrame(const AcquiredFrame&) = delete;
    AcquiredFrame& operator=(const AcquiredFrame&) = delete;
    AcquiredFrame(AcquiredFrame&& source) noexcept;
    AcquiredFrame& operator=(AcquiredFrame&& source) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint32_t image_index() const noexcept {
        return image_index_;
    }
    [[nodiscard]] VkCommandBuffer command_buffer() const noexcept {
        return command_buffer_;
    }
    [[nodiscard]] VkImage image() const noexcept { return image_; }
    [[nodiscard]] VkImageView image_view() const noexcept { return image_view_; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
    [[nodiscard]] std::optional<VkFramebuffer> framebuffer(
        std::span<const VkFramebuffer> framebuffers) const noexcept;

private:
    friend class VkSwapchain;

    AcquiredFrame(VkSwapchain* owner, std::uint64_t generation,
                  std::uint64_t serial, std::uint32_t frame_slot,
                  std::uint32_t image_index, const SwapFrame& frame,
                  VkImage image, VkImageView image_view,
                  VkExtent2D extent) noexcept;
    void invalidate() noexcept;

    VkSwapchain* owner_ = nullptr;
    std::uint64_t generation_ = 0;
    std::uint64_t serial_ = 0;
    std::uint32_t frame_slot_ = 0;
    std::uint32_t image_index_ = 0;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkFence fence_ = VK_NULL_HANDLE;
    VkSemaphore image_ready_ = VK_NULL_HANDLE;
    VkSemaphore render_done_ = VK_NULL_HANDLE;
};

/// Runtime-owned swapchain and per-frame synchronization.
class VkSwapchain final {
public:
    static constexpr std::uint32_t kMaxFrames = 2;

    VkSwapchain() = default;
    ~VkSwapchain() { shutdown(); }

    VkSwapchain(const VkSwapchain&) = delete;
    VkSwapchain& operator=(const VkSwapchain&) = delete;

    /// Transactionally creates the swapchain, image views and sync resources.
    [[nodiscard]] bool init(VkContext& ctx, std::uint32_t width,
                            std::uint32_t height);

    /// Builds a complete replacement before retiring the current state.
    [[nodiscard]] bool rebuild(VkContext& ctx, std::uint32_t width,
                               std::uint32_t height);

    /// Idempotent reverse-order teardown, valid after partial construction.
    void shutdown() noexcept;

    [[nodiscard]] bool is_ready() const noexcept {
        return state_.swapchain != VK_NULL_HANDLE && !state_.images.empty() &&
               state_.images.size() == state_.image_views.size();
    }

    [[nodiscard]] std::optional<AcquiredFrame> begin_frame(VkContext& ctx);

    /// Ends, submits and presents exactly the image represented by `frame`.
    /// The token is invalidated after every attempt, including failures.
    [[nodiscard]] bool end_frame(VkContext& ctx, AcquiredFrame& frame);

    [[nodiscard]] VkSwapchainKHR swapchain() const noexcept {
        return state_.swapchain;
    }
    [[nodiscard]] VkFormat format() const noexcept { return state_.format; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return state_.extent; }
    [[nodiscard]] std::uint32_t image_count() const noexcept {
        return static_cast<std::uint32_t>(state_.images.size());
    }
    [[nodiscard]] std::span<const VkImageView> image_views() const noexcept {
        return state_.image_views;
    }

private:
    friend class AcquiredFrame;
    friend struct VkSwapchainTestAccess;

    struct OwnedState {
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        VkExtent2D extent{};
        std::vector<VkImage> images;
        std::vector<VkImageView> image_views;
        std::array<SwapFrame, kMaxFrames> frames{};
    };

    [[nodiscard]] bool create_swapchain(VkContext& ctx, std::uint32_t width,
                                        std::uint32_t height,
                                        VkSwapchainKHR old_swapchain,
                                        OwnedState& output);
    [[nodiscard]] bool create_image_views(VkContext& ctx, OwnedState& output);
    [[nodiscard]] bool create_sync(VkContext& ctx, OwnedState& output);
    static void destroy_owned(
        VkDevice device,
        const ayther::runtime::vulkan::VulkanCalls& calls,
        OwnedState& state) noexcept;
    [[nodiscard]] bool accepts(const AcquiredFrame& frame) const noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    ayther::runtime::vulkan::VulkanCalls calls_{};
    OwnedState state_{};
    std::uint32_t frame_index_ = 0;
    std::uint64_t generation_ = 1;
    std::uint64_t next_serial_ = 1;
    std::uint64_t active_serial_ = 0;
};
