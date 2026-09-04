#include "vk_present.h"
#include "vulkan_backend/vk_context.h"
#include "vk_swapchain.h"
#include "aspect_fit.h"
#include "split_geometry.h"
#include "output_profile.h"   // #296: escalado y filtro del perfil   // #298: la geometria del split, testeable sin GPU

#include <cstdint>

// The acquired capability carries the exact command buffer, image and extent;
// presentation code has no unchecked access to swapchain-owned arrays.

// ---------------------------------------------------------------------------
// VkPresent::blit_to_swapchain
//   src:  Engine offscreen in its declared handoff layout
//   swap: UNDEFINED → TRANSFER_DST (left here; caller calls finalize())
// ---------------------------------------------------------------------------
void VkPresent::blit_to_swapchain(VkContext& ctx,
                                  const AcquiredFrame& frame,
                                  const ayther::engine::RenderImageView& src,
                                  bool integer, bool smooth) {
    if (!frame.valid() || !src.is_valid() ||
        src.queue_family_index != ctx.graphics_family()) return;
    const VkCommandBuffer cmd = frame.command_buffer();
    const VkImage swap_img = frame.image();

    // Borrowed source: declared handoff layout → TRANSFER_SRC. The view's
    // readiness masks are the source scope supplied by Engine.
    VkImageMemoryBarrier src_to_src{};
    src_to_src.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    src_to_src.oldLayout           = src.layout;
    src_to_src.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    src_to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_to_src.image               = src.image;
    src_to_src.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    src_to_src.srcAccessMask       = src.ready_access_mask;
    src_to_src.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        src.ready_stage_mask, VK_PIPELINE_STAGE_TRANSFER_BIT,
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
        prof, src.extent.width, src.extent.height,
        frame.extent().width, frame.extent().height);
    const FitRect fit{ r.x, r.y, r.w, r.h };

    VkImageBlit region{};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.srcOffsets[0]  = { 0, 0, 0 };
    region.srcOffsets[1]  = { static_cast<int32_t>(src.extent.width),
                              static_cast<int32_t>(src.extent.height), 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstOffsets[0]  = { fit.x, fit.y, 0 };
    region.dstOffsets[1]  = { fit.x + fit.w, fit.y + fit.h, 1 };

    vkCmdBlitImage(cmd,
        src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swap_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region, smooth ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);

    VkImageMemoryBarrier src_back{};
    src_back.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    src_back.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    src_back.newLayout           = src.layout;
    src_back.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_back.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    src_back.image               = src.image;
    src_back.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    src_back.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    src_back.dstAccessMask       = src.ready_access_mask;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, src.ready_stage_mask,
        0, 0, nullptr, 0, nullptr, 1, &src_back);

    // The borrowed source is restored; swap stays in TRANSFER_DST until finalize().
}

// ---------------------------------------------------------------------------
// VkPresent::blit_split_to_swapchain  (#298)
//   left/right: dos offscreen en su layout declarado (el original y el HD del
//   MISMO FrameView). Salen recortados a su lado del divisor.
// ---------------------------------------------------------------------------
void VkPresent::blit_split_to_swapchain(VkContext& ctx,
                                        const AcquiredFrame& frame,
                                        const ayther::engine::RenderImageView& left,
                                        const ayther::engine::RenderImageView& right,
                                        float split, bool vertical) {
    if (!frame.valid() || !left.is_valid() || !right.is_valid() ||
        left.queue_family_index != ctx.graphics_family() ||
        right.queue_family_index != ctx.graphics_family()) return;
    const VkCommandBuffer cmd = frame.command_buffer();
    const VkImage swap_img = frame.image();
    if (split < 0.0f) split = 0.0f;
    if (split > 1.0f) split = 1.0f;

    auto to_transfer_src = [&](const ayther::engine::RenderImageView& image) {
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = image.layout;
        b.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = image.image;
        b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.srcAccessMask       = image.ready_access_mask;
        b.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            image.ready_stage_mask, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
    };
    to_transfer_src(left);
    if (right.image != left.image) to_transfer_src(right);

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
    const FitRect fit = aspect_fit(left.extent.width, left.extent.height,
                                   frame.extent().width, frame.extent().height);

    // Un lado con 0 píxeles no es un blit válido —Vulkan lo rechaza— y además
    // no hay nada que dibujar: el divisor en un extremo es «mostrar sólo el
    // otro», que es exactamente lo que pasa al saltearlo.
    auto blit_side = [&](const ayther::engine::RenderImageView& image,
                         float from, float to) {
        const ayther::SplitRegion r = ayther::split_region(
            static_cast<int32_t>(image.extent.width),
            static_cast<int32_t>(image.extent.height),
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
            image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            swap_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &b, VK_FILTER_LINEAR);
    };
    blit_side(left, 0.0f, split);
    blit_side(right, split, 1.0f);

    // Las dos vuelven a su layout publicado: el renderer las entrega así y el
    // próximo frame parte de ahí. Dejarlas en TRANSFER_SRC haría que la
    // barrera del frame siguiente partiera de un layout que no es el real —
    // válido hoy y roto el día que alguien las samplee.
    auto restore_handoff = [&](const ayther::engine::RenderImageView& image) {
        VkImageMemoryBarrier b{};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.newLayout           = image.layout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = image.image;
        b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        b.dstAccessMask       = image.ready_access_mask;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, image.ready_stage_mask,
            0, 0, nullptr, 0, nullptr, 1, &b);
    };
    restore_handoff(left);
    if (right.image != left.image) restore_handoff(right);
    // swap queda en TRANSFER_DST — el caller llama a finalize().
}

// ---------------------------------------------------------------------------
// VkPresent::finalize
//   swap: TRANSFER_DST → PRESENT_SRC_KHR
// ---------------------------------------------------------------------------
void VkPresent::finalize(VkContext&, const AcquiredFrame& frame) {
    if (!frame.valid()) return;
    const VkCommandBuffer cmd = frame.command_buffer();
    const VkImage swap_img = frame.image();

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
