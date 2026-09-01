#pragma once
// ---------------------------------------------------------------------------
// vk_postprocess.h — CRT-style post-processing over the emulator framebuffer.
//                    Ayther v0.9.4
//
// Replaces VkPresent::blit() (NEAREST filter) when a post-process shader is
// available.  If init() fails (no SPIR-V found), the caller falls back to
// VkPresent::blit() transparently — no code change needed elsewhere.
//
// ## Layout transition chain  (same exit state as VkPresent::blit)
//
//   Swapchain (just acquired, UNDEFINED)
//     │  [render pass, initialLayout=UNDEFINED, loadOp=DONT_CARE]
//     ▼
//   COLOR_ATTACHMENT_OPTIMAL  (during the pass)
//     │  [render pass finalLayout auto-transition]
//     ▼
//   TRANSFER_DST_OPTIMAL      ← identical to VkPresent::blit() exit state
//     │  (tile blits + VkSprite::draw() + VkPresent::finalize() unchanged)
//     ▼
//   PRESENT_SRC_KHR
//
// ## Descriptor layout
//   set 0, binding 0 = combined image sampler  →  emu_tex (persistent)
//
// ## Push constants (fragment stage only, 8 × float = 32 bytes)
//   scr_w, scr_h    — swapchain dimensions
//   emu_h           — native emulator height (lines), e.g. 224 or 240
//   time            — elapsed seconds (for future animated effects)
//   crt_strength    — overall CRT effect intensity  [0, 1]
//   scan_strength   — scanline darkness              [0, 1]
//   vignette        — vignette intensity             [0, 1]
//   ntsc            — NTSC chroma bleed               [0, 1]  (#230 EM-7.2)
//
// ## Lifecycle
//   postprocess.init(ctx, swap, vert_spv, frag_spv, emu_tex)
//   postprocess.rebuild(ctx, swap)   // on SDL_EVENT_WINDOW_RESIZED
//   postprocess.shutdown(ctx)        // before vkDeviceWaitIdle / shutdown
// ---------------------------------------------------------------------------
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

class VkContext;
class VkSwapchain;

/// Output-profile destination rectangle in swapchain pixels (#296).
///
/// The caller computes this with the GPU-independent `ayther::output_rect`
/// policy. Keeping policy outside the Vulkan backend makes it directly testable.
///
/// Zero width or height requests the legacy 4:3 aspect-fit fallback.
struct OutDestRect { int32_t x = 0, y = 0, w = 0, h = 0; };

class VkPostProcess {
public:
    // ---- Lifecycle -----------------------------------------------------------

    /// Initialise the render pass, pipeline, sampler, and descriptor (unbound).
    /// Bind the sampled source with set_source() after init() + after resize.
    /// Returns false if SPIR-V shaders cannot be loaded — caller should fall back
    /// to VkPresent::blit_to_swapchain().
    bool init(VkContext& ctx, VkSwapchain& swap,
              const char* vert_spv_path, const char* frag_spv_path);

    /// Bind (or re-bind) the sampled source image — the renderer's offscreen HD
    /// frame, in SHADER_READ_ONLY_OPTIMAL. Call after init() and after every
    /// resize (the offscreen view changes when the target is recreated).
    void set_source(VkContext& ctx, VkImageView src_view);

    /// Recreate framebuffers after a swapchain resize.
    void rebuild(VkContext& ctx, VkSwapchain& swap);

    /// Destroy all Vulkan objects.
    void shutdown(VkContext& ctx);

    // ---- Per-frame apply -----------------------------------------------------

    /// Replace VkPresent::blit() — renders emu_tex through the post-process shader.
    ///
    /// Entry state: emu_tex in SHADER_READ_ONLY, swapchain just acquired (UNDEFINED).
    /// Exit  state: swapchain in TRANSFER_DST_OPTIMAL  (tile blits unchanged).
    ///
    ///  scr_w / scr_h   — current swapchain extent in pixels.
    ///  emu_h           — native emulator frame height (usually snap.h, 224 or 240).
    ///  time_s          — elapsed time in seconds.
    ///  crt_strength    — [0,1] overall CRT effect weight.
    ///  scan_strength   — [0,1] scanline darkness.
    ///  vignette        — [0,1] edge-darkening intensity.
    ///  ntsc            — [0,1] composite-video chroma bleed (#230 EM-7.2).
    ///                    It reuses the former `_pad` slot, preserving the push
    ///                    constant block size and pipeline-layout compatibility.
    ///
    ///  The 0.0 default and strict shader gate are intentional: disabled NTSC
    ///  processing remains bit-identical instead of incurring a lossy YIQ
    ///  round trip for a near-zero value.
    ///
    ///  dest   — output-profile rectangle; enables true integer scaling for
    ///           pixel-perfect presentation even when shaders are active.
    ///  smooth — `false` selects nearest-neighbor filtering; `true` selects
    ///           linear filtering.
    ///
    /// @pre init() and set_source() succeeded, `swap` has an acquired image,
    ///      and its command buffer is recording.
    void apply(VkContext& ctx, VkSwapchain& swap,
               float scr_w, float scr_h, float emu_h, float time_s,
               const OutDestRect& dest, bool smooth,
               float crt_strength, float scan_strength, float vignette,
               float ntsc = 0.0f);

    bool is_ready() const { return pipeline_ != VK_NULL_HANDLE; }

private:
    // ---- Vulkan objects ----
    VkRenderPass          render_pass_  = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_layout_  = VK_NULL_HANDLE;
    VkPipelineLayout      pipe_layout_  = VK_NULL_HANDLE;
    VkPipeline            pipeline_     = VK_NULL_HANDLE;
    // One sampler and descriptor set per filter. Descriptors may still be read
    // by an earlier in-flight frame, so filter selection binds a stable set
    // instead of rewriting a descriptor during the frame.
    VkSampler             sampler_smooth_ = VK_NULL_HANDLE;  // LINEAR
    VkSampler             sampler_sharp_  = VK_NULL_HANDLE;  // NEAREST
    VkDescriptorPool      desc_pool_    = VK_NULL_HANDLE;
    VkDescriptorSet       desc_smooth_  = VK_NULL_HANDLE;  // persistent, points at emu_tex
    VkDescriptorSet       desc_sharp_   = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> framebuffers_;
    uint32_t fb_w_ = 0, fb_h_ = 0;

    // ---- Helpers ----
    bool create_render_pass (VkContext& ctx, VkFormat fmt);
    bool create_pipeline    (VkContext& ctx,
                             const char* vert_spv_path,
                             const char* frag_spv_path);
    bool create_samplers    (VkContext& ctx);
    bool create_desc        (VkContext& ctx);   // pool + set (unbound; see set_source)
    void create_framebuffers(VkContext& ctx, VkSwapchain& swap);
    void destroy_framebuffers(VkContext& ctx);
};
