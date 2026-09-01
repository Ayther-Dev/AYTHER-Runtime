#pragma once
// ---------------------------------------------------------------------------
// VkPresent — blit the emulator (and HD tile) textures into the swapchain.
//
// v0.3.0: vkCmdBlitImage with NEAREST filter for the main emulator frame.
// v0.6.0: per-tile HD blits with LINEAR filter.
//
// ## Call order each frame (with tile substitution):
//
//   swapchain.begin_frame(ctx)
//   emu_tex.upload(ctx, cmd, ...)
//   VkPresent::blit(ctx, swap, emu_tex)         // full-frame blit; leaves
//                                                //  swap in TRANSFER_DST
//   for each tile sub:
//       VkPresent::blit_tile(ctx, swap, hd_tex, dst_x, dst_y, dst_w, dst_h)
//   VkPresent::finalize(ctx, swap)              // swap → PRESENT_SRC_KHR
//   swapchain.end_frame(ctx)
//
// ## Without tile substitution (legacy path):
//
//   swapchain.begin_frame(ctx)
//   emu_tex.upload(ctx, cmd, ...)
//   VkPresent::blit(ctx, swap, emu_tex)
//   VkPresent::finalize(ctx, swap)
//   swapchain.end_frame(ctx)
// ---------------------------------------------------------------------------
#include <vulkan/vulkan.h>
#include <cstdint>

class VkContext;
class VkSwapchain;
class VkTexture;

class VkPresent {
public:
    // -----------------------------------------------------------------------
    // Main emulator-frame blit
    //   src_tex: SHADER_READ_ONLY → TRANSFER_SRC → SHADER_READ_ONLY
    //   swap:    UNDEFINED        → TRANSFER_DST  (stays TRANSFER_DST!)
    // -----------------------------------------------------------------------
    static void blit(VkContext& ctx, VkSwapchain& swap, VkTexture& src_tex);

    // -----------------------------------------------------------------------
    // HD tile blit — overlay multiple regions using the SAME HD texture.
    //
    //   hd_tex: SHADER_READ_ONLY → TRANSFER_SRC → SHADER_READ_ONLY  (once)
    //   swap:   must already be in TRANSFER_DST (stays TRANSFER_DST)
    //
    // All `count` rects are blitted inside a single pair of pipeline barriers,
    // reducing barrier overhead from O(count) to O(1) when the same tile hash
    // appears at multiple screen positions.
    // Uses LINEAR filter for smooth HD upscaling.
    // -----------------------------------------------------------------------

    /// One destination rectangle in window pixels.
    struct TileBlitRect { uint32_t x, y, w, h; };

    static void blit_tiles(VkContext& ctx, VkSwapchain& swap, VkTexture& hd_tex,
                           const TileBlitRect* rects, uint32_t count);

    // -----------------------------------------------------------------------
    // Blit the engine's offscreen HD frame into the swapchain (R3.1).
    //   src:  an AytherRenderer offscreen image, already in TRANSFER_SRC_OPTIMAL
    //   swap: UNDEFINED → TRANSFER_DST  (stays TRANSFER_DST; caller finalizes)
    // LINEAR filter (a 1:1 copy when the canvas == swapchain extent).
    // -----------------------------------------------------------------------
    /// Blit the engine's offscreen image to the current swapchain image.
    ///
    /// `integer` uses an integer multiple of the native frame instead of the
    /// largest aspect-preserving fit, avoiding uneven pixel thickness in the
    /// pixel-perfect profile. `smooth` selects linear filtering when true and
    /// nearest-neighbor filtering otherwise.
    /// @pre `src` is in `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL` and its extent is
    ///      non-empty. The swapchain must have a currently acquired image.
    static void blit_to_swapchain(VkContext& ctx, VkSwapchain& swap,
                                  VkImage src, VkExtent2D src_extent,
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
    static void blit_split_to_swapchain(VkContext& ctx, VkSwapchain& swap,
                                        VkImage left,  VkExtent2D left_extent,
                                        VkImage right, VkExtent2D right_extent,
                                        float split, bool vertical);

    // -----------------------------------------------------------------------
    // Finalize — transition swapchain image from TRANSFER_DST → PRESENT_SRC.
    // Must be called once per frame after all blit and blit_tiles calls.
    // -----------------------------------------------------------------------
    static void finalize(VkContext& ctx, VkSwapchain& swap);
};
