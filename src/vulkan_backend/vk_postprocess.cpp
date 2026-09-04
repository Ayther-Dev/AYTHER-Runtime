// ---------------------------------------------------------------------------
// vk_postprocess.cpp — CRT-style post-process pass.  Ayther v0.9.4
//
// ## Render-pass layout chain
//
//   UNDEFINED (just acquired)
//     │  [render pass, initialLayout=UNDEFINED, loadOp=DONT_CARE]
//     ▼
//   COLOR_ATTACHMENT_OPTIMAL  (during subpass)
//     │  [render pass finalLayout auto-transition]
//     ▼
//   TRANSFER_DST_OPTIMAL      ← same exit state as the plain blit
//
// No explicit pre-pass barrier is needed; the render pass's initialLayout=UNDEFINED
// lets the driver skip any content preservation.  The subpass dependency (dep[0])
// pairs with the source handoff barrier recorded by Engine.
//
// ## Descriptor layout
//   set 0, binding 0 = combined image sampler (Engine view, rebound on resize)
//
// ## Push constants (frag stage, 8 × float = 32 bytes)
//   scr_w, scr_h, emu_h, time, crt_strength, scan_strength, vignette, ntsc
// ---------------------------------------------------------------------------

#include "vk_postprocess.h"
#include "vulkan_backend/spirv_file.h"
#include "vulkan_backend/vk_context.h"
#include "vk_swapchain.h"
#include "aspect_fit.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

static VkShaderModule pp_make_shader_module(VkContext& ctx,
                                             const std::vector<uint32_t>& code,
                                             const char* operation) {
    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size() * sizeof(uint32_t);
    info.pCode    = code.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    if (!ayther::runtime::vulkan::require_vk_success(
            operation, ctx.calls().create_shader_module(
                           ctx.device(), &info, nullptr, &mod))) {
        return VK_NULL_HANDLE;
    }
    return mod;
}

// ---------------------------------------------------------------------------
// VkPostProcess::create_render_pass
// ---------------------------------------------------------------------------
bool VkPostProcess::create_render_pass(VkContext& ctx, VkFormat fmt) {
    // Single attachment: the swapchain image.
    // initialLayout = UNDEFINED — content is don't-care (we overwrite it all).
    // finalLayout   = TRANSFER_DST — so tile blits work unchanged after us.
    VkAttachmentDescription att{};
    att.format         = fmt;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;      // black pillarbox/letterbox bars
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_ref;

    // dep[0]: emu_tex TRANSFER_WRITE (from upload) → frag shader read.
    // dep[1]: color-attachment write → subsequent TRANSFER_WRITE (tile blits).
    VkSubpassDependency deps[2]{};

    deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass      = 0;
    deps[0].srcStageMask    = VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[0].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;
    deps[0].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;

    deps[1].srcSubpass      = 0;
    deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask    = VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask   = VK_ACCESS_TRANSFER_WRITE_BIT;

    VkRenderPassCreateInfo rp_info{};
    rp_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.attachmentCount = 1;
    rp_info.pAttachments    = &att;
    rp_info.subpassCount    = 1;
    rp_info.pSubpasses      = &subpass;
    rp_info.dependencyCount = 2;
    rp_info.pDependencies   = deps;

    return ayther::runtime::vulkan::require_vk_success(
        "vkCreateRenderPass [VkPostProcess]",
        ctx.calls().create_render_pass(
            ctx.device(), &rp_info, nullptr, &render_pass_));
}

// ---------------------------------------------------------------------------
// VkPostProcess::create_pipeline
// ---------------------------------------------------------------------------
bool VkPostProcess::create_pipeline(VkContext& ctx,
                                     const char* vert_spv_path,
                                     const char* frag_spv_path) {
    const auto vert_binary =
        ayther::runtime::vulkan::load_spirv_binary(vert_spv_path);
    const auto frag_binary =
        ayther::runtime::vulkan::load_spirv_binary(frag_spv_path);
    if (!vert_binary || !frag_binary) {
        std::fprintf(stderr,
            "[VkPostProcess] Cannot read complete SPIR-V shaders: %s, %s\n",
            vert_spv_path, frag_spv_path);
        return false;
    }

    VkShaderModule vert_mod = pp_make_shader_module(
        ctx, vert_binary.words, "vkCreateShaderModule [vertex]");
    VkShaderModule frag_mod = pp_make_shader_module(
        ctx, frag_binary.words, "vkCreateShaderModule [fragment]");
    if (!vert_mod || !frag_mod) {
        if (vert_mod)
            ctx.calls().destroy_shader_module(ctx.device(), vert_mod, nullptr);
        if (frag_mod)
            ctx.calls().destroy_shader_module(ctx.device(), frag_mod, nullptr);
        return false;
    }

    // ---- Descriptor set layout: binding 0 = combined image sampler ----------
    VkDescriptorSetLayoutBinding sampler_binding{};
    sampler_binding.binding        = 0;
    sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_binding.descriptorCount= 1;
    sampler_binding.stageFlags     = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl_info{};
    dsl_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl_info.bindingCount = 1;
    dsl_info.pBindings    = &sampler_binding;

    if (!ayther::runtime::vulkan::require_vk_success(
            "vkCreateDescriptorSetLayout",
            ctx.calls().create_descriptor_set_layout(
                ctx.device(), &dsl_info, nullptr, &desc_layout_))) {
        ctx.calls().destroy_shader_module(ctx.device(), vert_mod, nullptr);
        ctx.calls().destroy_shader_module(ctx.device(), frag_mod, nullptr);
        return false;
    }

    // ---- Push constant range: 8 floats, fragment stage only -----------------
    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pc_range.offset     = 0;
    pc_range.size       = 8 * sizeof(float);  // scr_w,scr_h,emu_h,time,crt,scan,vig,ntsc

    VkPipelineLayoutCreateInfo pl_info{};
    pl_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl_info.setLayoutCount         = 1;
    pl_info.pSetLayouts            = &desc_layout_;
    pl_info.pushConstantRangeCount = 1;
    pl_info.pPushConstantRanges    = &pc_range;

    if (!ayther::runtime::vulkan::require_vk_success(
            "vkCreatePipelineLayout",
            ctx.calls().create_pipeline_layout(
                ctx.device(), &pl_info, nullptr, &pipe_layout_))) {
        ctx.calls().destroy_shader_module(ctx.device(), vert_mod, nullptr);
        ctx.calls().destroy_shader_module(ctx.device(), frag_mod, nullptr);
        return false;
    }

    // ---- Shader stages -------------------------------------------------------
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_mod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_mod;
    stages[1].pName  = "main";

    // ---- No vertex buffer — fullscreen triangle generated in vertex shader ---
    VkPipelineVertexInputStateCreateInfo vtx_input{};
    vtx_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_asm{};
    input_asm.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_asm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // ---- Dynamic viewport + scissor (survives resize without pipeline rebuild) -
    VkPipelineViewportStateCreateInfo vp_state{};
    vp_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp_state.viewportCount = 1;
    vp_state.scissorCount  = 1;

    VkDynamicState dyn_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates    = dyn_states;

    // ---- Rasterizer ----------------------------------------------------------
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ---- No blending: post-process overwrites the full frame -----------------
    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.blendEnable    = VK_FALSE;
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blend_att;

    // ---- Assemble -----------------------------------------------------------
    VkGraphicsPipelineCreateInfo gp_info{};
    gp_info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp_info.stageCount          = 2;
    gp_info.pStages             = stages;
    gp_info.pVertexInputState   = &vtx_input;
    gp_info.pInputAssemblyState = &input_asm;
    gp_info.pViewportState      = &vp_state;
    gp_info.pRasterizationState = &raster;
    gp_info.pMultisampleState   = &ms;
    gp_info.pColorBlendState    = &blend;
    gp_info.pDynamicState       = &dyn;
    gp_info.layout              = pipe_layout_;
    gp_info.renderPass          = render_pass_;
    gp_info.subpass             = 0;

    const VkResult pipeline_result = ctx.calls().create_graphics_pipelines(
        ctx.device(), VK_NULL_HANDLE, 1, &gp_info, nullptr, &pipeline_);

    ctx.calls().destroy_shader_module(ctx.device(), vert_mod, nullptr);
    ctx.calls().destroy_shader_module(ctx.device(), frag_mod, nullptr);

    return ayther::runtime::vulkan::require_vk_success(
        "vkCreateGraphicsPipelines", pipeline_result);
}

// ---------------------------------------------------------------------------
// VkPostProcess::create_sampler
// ---------------------------------------------------------------------------
bool VkPostProcess::create_samplers(VkContext& ctx) {
    // Uno por filtro: el perfil de salida decide cuál se usa (#296). Antes era
    // LINEAR fijo y «LCD nativo» —que por definición es sin filtro— salía
    // suavizado igual que «Suavizado».
    auto crear = [&](VkFilter filtro, VkSampler* out) {
        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = filtro;
        si.minFilter    = filtro;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.minLod       = 0.0f;
        si.maxLod       = 0.25f;  // no mipmaps
        return ayther::runtime::vulkan::require_vk_success(
            filtro == VK_FILTER_LINEAR
                ? "vkCreateSampler [linear]"
                : "vkCreateSampler [nearest]",
            ctx.calls().create_sampler(ctx.device(), &si, nullptr, out));
    };
    return crear(VK_FILTER_LINEAR,  &sampler_smooth_)
        && crear(VK_FILTER_NEAREST, &sampler_sharp_);
}

// ---------------------------------------------------------------------------
// VkPostProcess::create_desc
//   Creates a single descriptor pool + set pointing at emu_tex.
//   Written once; valid for the lifetime of emu_tex (never updated).
// ---------------------------------------------------------------------------
bool VkPostProcess::create_desc(VkContext& ctx) {
    // Pool: dos combined image samplers, uno por filtro.
    VkDescriptorPoolSize pool_size{};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 2;

    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets       = 2;
    pi.poolSizeCount = 1;
    pi.pPoolSizes    = &pool_size;

    if (!ayther::runtime::vulkan::require_vk_success(
            "vkCreateDescriptorPool",
            ctx.calls().create_descriptor_pool(
                ctx.device(), &pi, nullptr, &desc_pool_))) {
        return false;
    }

    // Allocate both descriptor sets (bound to a source later by set_source()).
    const VkDescriptorSetLayout layouts[2] = { desc_layout_, desc_layout_ };
    VkDescriptorSet sets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool     = desc_pool_;
    alloc_info.descriptorSetCount = 2;
    alloc_info.pSetLayouts        = layouts;

    if (!ayther::runtime::vulkan::require_vk_success(
            "vkAllocateDescriptorSets",
            ctx.calls().allocate_descriptor_sets(
                ctx.device(), &alloc_info, sets))) {
        return false;
    }
    desc_smooth_ = sets[0];
    desc_sharp_  = sets[1];
    return true;
}

void VkPostProcess::set_source(
    VkContext& ctx, const ayther::engine::RenderImageView& source) {
    if (!source.is_valid() ||
        source.queue_family_index != ctx.graphics_family() ||
        (source.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
         source.layout != VK_IMAGE_LAYOUT_GENERAL)) return;

    // Bind the combined image sampler to the Engine-borrowed source. Safe on
    // resize because callers wait idle before the old view is destroyed.
    // Los DOS sets apuntan a la misma imagen y se diferencian sólo en el
    // sampler: escribir uno y olvidarse del otro dejaría el filtro que no se
    // actualizó apuntando a una vista destruida en el próximo resize.
    VkDescriptorImageInfo img_info[2]{};
    img_info[0].sampler     = sampler_smooth_;
    img_info[0].imageView   = source.image_view;
    img_info[0].imageLayout = source.layout;
    img_info[1]             = img_info[0];
    img_info[1].sampler     = sampler_sharp_;

    VkWriteDescriptorSet write[2]{};
    for (int i = 0; i < 2; ++i) {
        write[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write[i].dstSet          = i == 0 ? desc_smooth_ : desc_sharp_;
        write[i].dstBinding      = 0;
        write[i].descriptorCount = 1;
        write[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write[i].pImageInfo      = &img_info[i];
    }

    vkUpdateDescriptorSets(ctx.device(), 2, write, 0, nullptr);
}

// ---------------------------------------------------------------------------
// VkPostProcess::create_framebuffers / destroy_framebuffers
// ---------------------------------------------------------------------------
bool VkPostProcess::create_framebuffers(
    VkContext& ctx, VkSwapchain& swap,
    std::vector<VkFramebuffer>& output) const {
    const auto views = swap.image_views();
    output.resize(views.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0; i < views.size(); ++i) {
        const VkImageView view = views[i];
        VkFramebufferCreateInfo fi{};
        fi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass      = render_pass_;
        fi.attachmentCount = 1;
        fi.pAttachments    = &view;
        fi.width           = fb_w_;
        fi.height          = fb_h_;
        fi.layers          = 1;
        if (!ayther::runtime::vulkan::require_vk_success(
                "vkCreateFramebuffer [VkPostProcess]",
                ctx.calls().create_framebuffer(
                    ctx.device(), &fi, nullptr, &output[i]))) {
            return false;
        }
    }
    return true;
}

void VkPostProcess::destroy_framebuffers(VkContext& ctx) {
    for (auto fb : framebuffers_) {
        if (fb != VK_NULL_HANDLE)
            ctx.calls().destroy_framebuffer(ctx.device(), fb, nullptr);
    }
    framebuffers_.clear();
    fb_w_ = fb_h_ = 0;
}

// ---------------------------------------------------------------------------
// VkPostProcess::init / rebuild / shutdown
// ---------------------------------------------------------------------------
bool VkPostProcess::init(VkContext& ctx, VkSwapchain& swap,
                          const char* vert_spv_path, const char* frag_spv_path) {
    if (!ctx.is_ready() || !swap.is_ready()) return false;
    if (is_ready()) return true;

    VkPostProcess pending;
    const bool complete =
        pending.create_render_pass(ctx, swap.format()) &&
        pending.create_pipeline(ctx, vert_spv_path, frag_spv_path) &&
        pending.create_samplers(ctx) && pending.create_desc(ctx) &&
        pending.create_framebuffers(ctx, swap, pending.framebuffers_);
    if (!complete) {
        pending.shutdown(ctx);
        return false;
    }
    pending.fb_w_ = swap.extent().width;
    pending.fb_h_ = swap.extent().height;
    shutdown(ctx);
    swap_state(pending);

    std::fprintf(stdout,
        "[VkPostProcess] Post-process pipeline ready  (%ux%u)\n",
        fb_w_, fb_h_);
    return true;
}

bool VkPostProcess::rebuild(VkContext& ctx, VkSwapchain& swap) {
    if (!pipeline_ || !swap.is_ready()) return false;
    if (const auto failure =
            ctx.wait_idle("vkDeviceWaitIdle [VkPostProcess::rebuild]")) {
        ayther::runtime::vulkan::log_vk_failure(*failure);
        return false;
    }
    std::vector<VkFramebuffer> pending;
    if (!create_framebuffers(ctx, swap, pending)) {
        for (const VkFramebuffer framebuffer : pending) {
            if (framebuffer != VK_NULL_HANDLE) {
                ctx.calls().destroy_framebuffer(
                    ctx.device(), framebuffer, nullptr);
            }
        }
        return false;
    }
    destroy_framebuffers(ctx);
    framebuffers_ = std::move(pending);
    fb_w_ = swap.extent().width;
    fb_h_ = swap.extent().height;
    std::fprintf(stdout,
        "[VkPostProcess] Rebuilt framebuffers  (%ux%u)\n",
        fb_w_, fb_h_);
    return true;
}

void VkPostProcess::shutdown(VkContext& ctx) {
    if (!ctx.is_ready()) return;
    if (const auto failure =
            ctx.wait_idle("vkDeviceWaitIdle [VkPostProcess::shutdown]")) {
        ayther::runtime::vulkan::log_vk_failure(*failure);
    }

    destroy_framebuffers(ctx);

    if (desc_pool_ != VK_NULL_HANDLE) {
        ctx.calls().destroy_descriptor_pool(ctx.device(), desc_pool_, nullptr);
        desc_pool_ = VK_NULL_HANDLE;
    }
    if (sampler_smooth_ != VK_NULL_HANDLE) {
        ctx.calls().destroy_sampler(ctx.device(), sampler_smooth_, nullptr);
        sampler_smooth_ = VK_NULL_HANDLE;
    }
    if (sampler_sharp_ != VK_NULL_HANDLE) {
        ctx.calls().destroy_sampler(ctx.device(), sampler_sharp_, nullptr);
        sampler_sharp_ = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        ctx.calls().destroy_pipeline(ctx.device(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipe_layout_ != VK_NULL_HANDLE) {
        ctx.calls().destroy_pipeline_layout(ctx.device(), pipe_layout_, nullptr);
        pipe_layout_ = VK_NULL_HANDLE;
    }
    if (desc_layout_ != VK_NULL_HANDLE) {
        ctx.calls().destroy_descriptor_set_layout(
            ctx.device(), desc_layout_, nullptr);
        desc_layout_ = VK_NULL_HANDLE;
    }
    if (render_pass_ != VK_NULL_HANDLE) {
        ctx.calls().destroy_render_pass(ctx.device(), render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
    desc_smooth_ = VK_NULL_HANDLE;  // freed with the pool
    desc_sharp_  = VK_NULL_HANDLE;
}

void VkPostProcess::swap_state(VkPostProcess& other) noexcept {
    using std::swap;
    swap(render_pass_, other.render_pass_);
    swap(desc_layout_, other.desc_layout_);
    swap(pipe_layout_, other.pipe_layout_);
    swap(pipeline_, other.pipeline_);
    swap(sampler_smooth_, other.sampler_smooth_);
    swap(sampler_sharp_, other.sampler_sharp_);
    swap(desc_pool_, other.desc_pool_);
    swap(desc_smooth_, other.desc_smooth_);
    swap(desc_sharp_, other.desc_sharp_);
    swap(framebuffers_, other.framebuffers_);
    swap(fb_w_, other.fb_w_);
    swap(fb_h_, other.fb_h_);
}

// ---------------------------------------------------------------------------
// VkPostProcess::apply
// ---------------------------------------------------------------------------
void VkPostProcess::apply(VkContext&, const AcquiredFrame& frame,
                           float scr_w, float scr_h, float emu_h, float time_s,
                           const OutDestRect& dest, bool smooth,
                           float crt_strength, float scan_strength, float vignette,
                           float ntsc) {
    const auto framebuffer = frame.framebuffer(framebuffers_);
    if (!pipeline_ || !framebuffer) return;

    const VkCommandBuffer cmd = frame.command_buffer();

    // No explicit layout barrier before BeginRenderPass:
    // initialLayout = UNDEFINED means the driver transitions the image as part
    // of the render pass begin — no prior content to preserve.

    // ---- Begin render pass --------------------------------------------------
    // loadOp=CLEAR fills the whole image black; the CRT triangle then draws into
    // the aspect-correct centered viewport, leaving black pillarbox/letterbox bars.
    VkClearValue clear_val{};
    clear_val.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass      = render_pass_;
    rp_begin.framebuffer     = *framebuffer;
    rp_begin.renderArea      = { {0, 0}, frame.extent() };
    rp_begin.clearValueCount = 1;
    rp_begin.pClearValues    = &clear_val;

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    // ---- Dynamic viewport + scissor: el rect DEL PERFIL ---------------------
    // The fullscreen triangle samples the whole offscreen across this rect, so
    // the content lands centred with black bars (vs. stretching to fill).
    //
    // #296: el rect lo decide el PERFIL DE SALIDA y llega resuelto. Acá se
    // calculaba siempre un fit 4:3 propio, así que «Pixel-perfect» no escalaba
    // por múltiplo entero en cuanto los shaders estaban presentes — o sea
    // siempre. El fit 4:3 queda de fallback para quien no pase rect.
    FitRect fit = { dest.x, dest.y, dest.w, dest.h };
    if (fit.w <= 0 || fit.h <= 0)
        fit = aspect_fit(4, 3, frame.extent().width, frame.extent().height);
    VkViewport vp{};
    vp.x        = static_cast<float>(fit.x);
    vp.y        = static_cast<float>(fit.y);
    vp.width    = static_cast<float>(fit.w);
    vp.height   = static_cast<float>(fit.h);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{ { fit.x, fit.y },
                      { static_cast<uint32_t>(fit.w), static_cast<uint32_t>(fit.h) } };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // ---- Bind pipeline + descriptor set -------------------------------------
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    // El filtro es el del perfil: se elige el set, no se reescribe el
    // descriptor — reescribirlo en medio de un frame lo cambiaría debajo del
    // frame anterior, que todavía lo está leyendo.
    const VkDescriptorSet set = smooth ? desc_smooth_ : desc_sharp_;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipe_layout_, 0, 1, &set, 0, nullptr);

    // ---- Push constants (frag stage, 8 × float) ----------------------------
    struct PC {
        float scr_w, scr_h, emu_h, time;
        float crt_strength, scan_strength, vignette, ntsc;
    };
    const PC pc{ scr_w, scr_h, emu_h, time_s,
                 crt_strength, scan_strength, vignette, ntsc };
    vkCmdPushConstants(cmd, pipe_layout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PC), &pc);

    // ---- Draw fullscreen triangle (3 vertices, no vertex buffer) ------------
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // ---- End render pass ---------------------------------------------------
    // finalLayout auto-transition: COLOR_ATTACHMENT_OPTIMAL → TRANSFER_DST_OPTIMAL.
    // The Runtime overlay and VkPresent::finalize() expect TRANSFER_DST_OPTIMAL.
    vkCmdEndRenderPass(cmd);
}
