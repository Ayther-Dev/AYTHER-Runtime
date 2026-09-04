#pragma once
// ---------------------------------------------------------------------------
// VkPresent — blit Engine-borrowed render images into the swapchain.
//
// ## Call order each frame:
//
//   swapchain.begin_frame(ctx)
//   renderer.render(ctx, cmd, ...)
//   VkPresent::blit_to_swapchain(ctx, frame, renderer.render_image())
//   VkPresent::finalize(ctx, frame)
//   swapchain.end_frame(ctx, frame)
// ---------------------------------------------------------------------------
#include <vulkan/vulkan.h>
#include <cstdint>

#include <ayther/engine/vulkan_interop.hpp>

class VkContext;
class AcquiredFrame;

class VkPresent {
public:
    // -----------------------------------------------------------------------
    // Blit the engine's offscreen HD frame into the swapchain (R3.1).
    //   src:  an Engine-borrowed image in the layout declared by the view
    //   swap: UNDEFINED → TRANSFER_DST  (stays TRANSFER_DST; caller finalizes)
    // LINEAR filter (a 1:1 copy when the canvas == swapchain extent).
    // -----------------------------------------------------------------------
    /// Blit the engine's offscreen image to the current swapchain image.
    ///
    /// `integer` uses an integer multiple of the native frame instead of the
    /// largest aspect-preserving fit, avoiding uneven pixel thickness in the
    /// pixel-perfect profile. `smooth` selects linear filtering when true and
    /// nearest-neighbor filtering otherwise.
    /// @pre `src` is valid and its owning queue family matches the recording
    ///      command buffer. The swapchain must have a currently acquired image.
    /// @post `src` is restored to its declared handoff layout.
    static void blit_to_swapchain(VkContext& ctx,
                                  const AcquiredFrame& frame,
                                  const ayther::engine::RenderImageView& src,
                                  bool integer = false, bool smooth = true);

    // -----------------------------------------------------------------------
    // #298 — synchronized split comparison.
    //
    // Two region blits target the same swapchain image without shaders or a
    // second composition pass. Both halves derive from the same `FrameView` and
    // command buffer, so they cannot advance independently.
    //
    // `split` is clamped to [0, 1]. With `vertical == false`, a vertical divider
    // separates left and right images. Each image is cropped rather than scaled
    // into half of the window, preserving the unsplit image geometry.
    static void blit_split_to_swapchain(VkContext& ctx,
                                        const AcquiredFrame& frame,
                                        const ayther::engine::RenderImageView& left,
                                        const ayther::engine::RenderImageView& right,
                                        float split, bool vertical);

    // -----------------------------------------------------------------------
    // Finalize — transition swapchain image from TRANSFER_DST → PRESENT_SRC.
    // Must be called once per frame after all blit and blit_tiles calls.
    // -----------------------------------------------------------------------
    static void finalize(VkContext& ctx, const AcquiredFrame& frame);
};
