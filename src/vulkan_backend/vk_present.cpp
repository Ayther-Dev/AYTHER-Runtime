#include "vk_present.h"
#include "vulkan_backend/vk_context.h"
#include "vk_swapchain.h"
#include "vulkan_backend/vk_texture.h"
#include "aspect_fit.h"
#include "split_geometry.h"
#include "output_profile.h"   // #296: escalado y filtro del perfil   // #298: la geometria del split, testeable sin GPU

// Note: the current swapchain VkImage is cached in VkSwapchain::images_ —
// we fetch it via swap.current_image().  This removes the 6 redundant
// vkGetSwapchainImagesKHR calls per frame the old helper used to do.

// ---------------------------------------------------------------------------
// VkPresent::blit
//   emu_tex:  SHADER_READ_ONLY → TRANSFER_SRC → SHADER_READ_ONLY
//   swap img: UNDEFINED        → TRANSFER_DST  (intentionally left here)
// ---------------------------------------------------------------------------
void VkPresent::blit(VkContext& ctx, VkSwapchain& swap, VkTexture& tex) {
    VkCommandBuffer cmd      = swap.current_frame().cmd;
    VkImage         swap_img = swap.current_image();

    // ---- 1. Transition tex: SHADER_READ_ONLY → TRANSFER_SRC ---------------
    VkImageMemoryBarrier to_src{};
    to_src.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_src.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_src.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_src.image               = tex.image();
    to_src.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    to_src.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    to_src.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &to_src);

    // ---- 2. Transition swap: UNDEFINED → TRANSFER_DST ---------------------
    VkImageMemoryBarrier swap_to_dst{};
    swap_to_dst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swap_to_dst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    swap_to_dst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swap_to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swap_to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swap_to_dst.image               = swap_img;
    swap_to_dst.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    swap_to_dst.srcAccessMask       = 0;
    swap_to_dst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &swap_to_dst);

    // ---- 3. Full-frame blit (NEAREST — integer-scale emu output) ----------
    VkImageBlit region{};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.srcOffsets[0]  = { 0, 0, 0 };
    region.srcOffsets[1]  = {
        static_cast<int32_t>(tex.width()),
        static_cast<int32_t>(tex.height()),
        1
    };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstOffsets[0]  = { 0, 0, 0 };
    region.dstOffsets[1]  = {
        static_cast<int32_t>(swap.extent().width),
        static_cast<int32_t>(swap.extent().height),
        1
    };

    vkCmdBlitImage(cmd,
        tex.image(),  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swap_img,     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region,
        VK_FILTER_NEAREST);

    // ---- 4. Transition tex back: TRANSFER_SRC → SHADER_READ_ONLY ----------
    VkImageMemoryBarrier tex_back{};
    tex_back.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    tex_back.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    tex_back.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    tex_back.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    tex_back.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    tex_back.image               = tex.image();
    tex_back.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    tex_back.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    tex_back.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &tex_back);

    // swap image intentionally left in TRANSFER_DST — caller calls finalize().
}

// ---------------------------------------------------------------------------
// VkPresent::blit_tiles
//   hd_tex: SHADER_READ_ONLY → TRANSFER_SRC → SHADER_READ_ONLY  (once total)
//   swap:   must be in TRANSFER_DST; stays TRANSFER_DST
//
// All `count` destination rectangles are blitted in a single barrier pair.
// This collapses N×(2 barriers + blit) into 2 barriers + N blits, so repeated
// occurrences of the same tile hash on screen cost O(1) transitions instead
// of O(N) — critical for tiles like "ground" that appear hundreds of times.
// ---------------------------------------------------------------------------
void VkPresent::blit_tiles(VkContext& ctx, VkSwapchain& swap, VkTexture& hd_tex,
                            const TileBlitRect* rects, uint32_t count) {
    if (count == 0) return;

    VkCommandBuffer cmd      = swap.current_frame().cmd;
    VkImage         swap_img = swap.current_image();

    // ---- 1. Transition hd_tex: SHADER_READ_ONLY → TRANSFER_SRC (once) -----
    VkImageMemoryBarrier to_src{};
    to_src.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_src.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_src.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_src.image               = hd_tex.image();
    to_src.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    to_src.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    to_src.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &to_src);

    // ---- 2. One blit per destination rect — no barriers between them -------
    for (uint32_t i = 0; i < count; ++i) {
        VkImageBlit region{};
        region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.srcOffsets[0]  = { 0, 0, 0 };
        region.srcOffsets[1]  = {
            static_cast<int32_t>(hd_tex.width()),
            static_cast<int32_t>(hd_tex.height()),
            1
        };
        region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.dstOffsets[0]  = {
            static_cast<int32_t>(rects[i].x),
            static_cast<int32_t>(rects[i].y),
            0
        };
        region.dstOffsets[1]  = {
            static_cast<int32_t>(rects[i].x + rects[i].w),
            static_cast<int32_t>(rects[i].y + rects[i].h),
            1
        };

        vkCmdBlitImage(cmd,
            hd_tex.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            swap_img,       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region,
            VK_FILTER_LINEAR);
    }

    // ---- 3. Transition hd_tex back: TRANSFER_SRC → SHADER_READ_ONLY (once) -
    VkImageMemoryBarrier tex_back{};
    tex_back.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    tex_back.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    tex_back.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    tex_back.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    tex_back.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    tex_back.image               = hd_tex.image();
    tex_back.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    tex_back.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    tex_back.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &tex_back);
}

// ---------------------------------------------------------------------------
// VkPresent::blit_to_swapchain
//   src:  offscreen HD frame, already in TRANSFER_SRC_OPTIMAL (AytherRenderer)
//   swap: UNDEFINED → TRANSFER_DST (left here; caller calls finalize())
// ---------------------------------------------------------------------------
void VkPresent::blit_to_swapchain(VkContext& ctx, VkSwapchain& swap,
                                  VkImage src, VkExtent2D src_extent,
                                  bool integer, bool smooth) {
    (void)ctx;
    VkCommandBuffer cmd      = swap.current_frame().cmd;
    VkImage         swap_img = swap.current_image();

    // src (offscreen): SHADER_READ_ONLY → TRANSFER_SRC. The renderer leaves the
    // offscreen in SHADER_READ so it can also be sampled (CRT / Lab viewport).
    VkImageMemoryBarrier src_to_src{};
    src_to_src.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    src_to_src.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    src_to_src.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    src_to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_to_src.image               = src;
    src_to_src.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    src_to_src.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    src_to_src.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &src_to_src);

    // swap: UNDEFINED → TRANSFER_DST
    VkImageMemoryBarrier to_dst{};
    to_dst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_dst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    to_dst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_dst.image               = swap_img;
    to_dst.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    to_dst.srcAccessMask       = 0;
    to_dst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &to_dst);

    // Clear the whole swapchain image to black first, so the pillarbox /
    // letterbox bars (the margin the aspect-fit blit doesn't cover) are black.
    const VkClearColorValue black{ { 0.0f, 0.0f, 0.0f, 1.0f } };
    const VkImageSubresourceRange full_range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(cmd, swap_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &black, 1, &full_range);

    // Aspect-correct blit src → a centered rect (4:3 content, black bars around).
    // #296: el perfil de salida decide como se escala.
    const ayther::runtime::OutputProfile prof{
        "", "", integer ? ayther::runtime::OutputScaling::Integer
                          : ayther::runtime::OutputScaling::Fit,
        smooth, 0.0f, 0.0f, 0.0f };
    const ayther::runtime::OutputRect r = ayther::runtime::output_rect(
        prof, src_extent.width, src_extent.height,
        swap.extent().width, swap.extent().height);
    const FitRect fit{ r.x, r.y, r.w, r.h };

    VkImageBlit region{};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.srcOffsets[0]  = { 0, 0, 0 };
    region.srcOffsets[1]  = { static_cast<int32_t>(src_extent.width),
                              static_cast<int32_t>(src_extent.height), 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstOffsets[0]  = { fit.x, fit.y, 0 };
    region.dstOffsets[1]  = { fit.x + fit.w, fit.y + fit.h, 1 };

    vkCmdBlitImage(cmd,
        src,      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swap_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region, smooth ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);

    // swap left in TRANSFER_DST — caller calls finalize().
}

// ---------------------------------------------------------------------------
// VkPresent::blit_split_to_swapchain  (#298)
//   left/right: dos offscreen en SHADER_READ_ONLY (el original y el HD del
//   MISMO FrameView). Salen recortados a su lado del divisor.
// ---------------------------------------------------------------------------
void VkPresent::blit_split_to_swapchain(VkContext& ctx, VkSwapchain& swap,
                                        VkImage left,  VkExtent2D left_extent,
                                        VkImage right, VkExtent2D right_extent,
                                        float split, bool vertical) {
    (void)ctx;
    if (left == VK_NULL_HANDLE || right == VK_NULL_HANDLE) return;
    VkCommandBuffer cmd      = swap.current_frame().cmd;
    VkImage         swap_img = swap.current_image();
    if (split < 0.0f) split = 0.0f;
    if (split > 1.0f) split = 1.0f;

    auto to_transfer_src = [&](VkImage img) {
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = img;
        b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        b.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
    };
    to_transfer_src(left);
    if (right != left) to_transfer_src(right);

    VkImageMemoryBarrier to_dst{};
    to_dst.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_dst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    to_dst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_dst.image               = swap_img;
    to_dst.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    to_dst.srcAccessMask       = 0;
    to_dst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &to_dst);

    const VkClearColorValue black{ { 0.0f, 0.0f, 0.0f, 1.0f } };
    const VkImageSubresourceRange full_range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(cmd, swap_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &black, 1, &full_range);

    // El mismo rect que usaría el blit sin dividir: las dos mitades caen donde
    // caería la imagen entera, sólo que cada una muestra su parte.
    const FitRect fit = aspect_fit(left_extent.width, left_extent.height,
                                   swap.extent().width, swap.extent().height);

    // Un lado con 0 píxeles no es un blit válido —Vulkan lo rechaza— y además
    // no hay nada que dibujar: el divisor en un extremo es «mostrar sólo el
    // otro», que es exactamente lo que pasa al saltearlo.
    auto blit_side = [&](VkImage src, VkExtent2D ext, float from, float to) {
        const ayther::SplitRegion r = ayther::split_region(
            static_cast<int32_t>(ext.width), static_cast<int32_t>(ext.height),
            fit.x, fit.y, fit.w, fit.h, from, to, vertical);
        if (!r.valid) return;
        VkImageBlit b{};
        b.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        b.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        b.srcOffsets[0]  = { r.sx0, r.sy0, 0 };
        b.srcOffsets[1]  = { r.sx1, r.sy1, 1 };
        b.dstOffsets[0]  = { r.dx0, r.dy0, 0 };
        b.dstOffsets[1]  = { r.dx1, r.dy1, 1 };
        vkCmdBlitImage(cmd,
            src,      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            swap_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &b, VK_FILTER_LINEAR);
    };
    blit_side(left,  left_extent,  0.0f,  split);
    blit_side(right, right_extent, split, 1.0f);

    // Las dos vuelven a SHADER_READ: el renderer las deja así y el próximo
    // frame las barrera desde ahí. Dejarlas en TRANSFER_SRC haría que la
    // barrera del frame siguiente partiera de un layout que no es el real —
    // válido hoy y roto el día que alguien las samplee.
    auto back_to_read = [&](VkImage img) {
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = img;
        b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        b.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
    };
    back_to_read(left);
    if (right != left) back_to_read(right);
    // swap queda en TRANSFER_DST — el caller llama a finalize().
}

// ---------------------------------------------------------------------------
// VkPresent::finalize
//   swap: TRANSFER_DST → PRESENT_SRC_KHR
// ---------------------------------------------------------------------------
void VkPresent::finalize(VkContext& ctx, VkSwapchain& swap) {
    VkCommandBuffer cmd      = swap.current_frame().cmd;
    VkImage         swap_img = swap.current_image();

    VkImageMemoryBarrier to_present{};
    to_present.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_present.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_present.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_present.image               = swap_img;
    to_present.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    to_present.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_present.dstAccessMask       = 0;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &to_present);
}
