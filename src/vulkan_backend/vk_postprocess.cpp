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
#include "vulkan_backend/vk_context.h"
#include "vk_swapchain.h"
#include "aspect_fit.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Internal file-local helpers (same pattern as vk_sprite.cpp)
// ---------------------------------------------------------------------------
// (the per-swap-image lookup helper is gone — use swap.current_image() directly.)

static std::vector<uint32_t> pp_load_spv(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "[VkPostProcess] Cannot open shader: %s\n", path);
        return {};
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::rewind(f);
    if (sz <= 0 || (sz % 4) != 0) {
        std::fprintf(stderr,
            "[VkPostProcess] Bad SPIR-V size (%ld) for: %s\n", sz, path);
        std::fclose(f);
        return {};
    }
    std::vector<uint32_t> code(static_cast<size_t>(sz) / 4);
    std::fread(code.data(), 1, static_cast<size_t>(sz), f);
    std::fclose(f);
    return code;
}

static VkShaderModule pp_make_shader_module(VkContext& ctx,
                                             const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size() * sizeof(uint32_t);
    info.pCode    = code.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    vkCreateShaderModule(ctx.device(), &info, nullptr, &mod);
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

    if (vkCreateRenderPass(ctx.device(), &rp_info, nullptr, &render_pass_) != VK_SUCCESS) {
        std::fprintf(stderr, "[VkPostProcess] vkCreateRenderPass failed\n");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// VkPostProcess::create_pipeline
// ---------------------------------------------------------------------------
bool VkPostProcess::create_pipeline(VkContext& ctx,
                                     const char* vert_spv_path,
                                     const char* frag_spv_path) {
    auto vert_code = pp_load_spv(vert_spv_path);
    auto frag_code = pp_load_spv(frag_spv_path);
    if (vert_code.empty() || frag_code.empty()) return false;

    VkShaderModule vert_mod = pp_make_shader_module(ctx, vert_code);
    VkShaderModule frag_mod = pp_make_shader_module(ctx, frag_code);
    if (!vert_mod || !frag_mod) {
        if (vert_mod) vkDestroyShaderModule(ctx.device(), vert_mod, nullptr);
        if (frag_mod) vkDestroyShaderModule(ctx.device(), frag_mod, nullptr);
        std::fprintf(stderr, "[VkPostProcess] vkCreateShaderModule failed\n");
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

    if (vkCreateDescriptorSetLayout(ctx.device(), &dsl_info,
                                    nullptr, &desc_layout_) != VK_SUCCESS) {
        vkDestroyShaderModule(ctx.device(), vert_mod, nullptr);
        vkDestroyShaderModule(ctx.device(), frag_mod, nullptr);
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

    if (vkCreatePipelineLayout(ctx.device(), &pl_info,
                               nullptr, &pipe_layout_) != VK_SUCCESS) {
        vkDestroyShaderModule(ctx.device(), vert_mod, nullptr);
        vkDestroyShaderModule(ctx.device(), frag_mod, nullptr);
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

    VkResult res = vkCreateGraphicsPipelines(ctx.device(), VK_NULL_HANDLE,
                                              1, &gp_info, nullptr, &pipeline_);

    vkDestroyShaderModule(ctx.device(), vert_mod, nullptr);
    vkDestroyShaderModule(ctx.device(), frag_mod, nullptr);

    if (res != VK_SUCCESS) {
        std::fprintf(stderr, "[VkPostProcess] vkCreateGraphicsPipelines failed (%d)\n", res);
        return false;
    }
    return true;
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
        if (vkCreateSampler(ctx.device(), &si, nullptr, out) != VK_SUCCESS) {
            std::fprintf(stderr, "[VkPostProcess] vkCreateSampler failed\n");
            return false;
        }
        return true;
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

    if (vkCreateDescriptorPool(ctx.device(), &pi, nullptr, &desc_pool_) != VK_SUCCESS) {
        std::fprintf(stderr, "[VkPostProcess] vkCreateDescriptorPool failed\n");
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

    if (vkAllocateDescriptorSets(ctx.device(), &alloc_info, sets) != VK_SUCCESS) {
        std::fprintf(stderr, "[VkPostProcess] vkAllocateDescriptorSets failed\n");
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
void VkPostProcess::create_framebuffers(VkContext& ctx, VkSwapchain& swap) {
    fb_w_ = swap.extent().width;
    fb_h_ = swap.extent().height;

    const uint32_t n = swap.image_count();
    framebuffers_.resize(n, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < n; ++i) {
        VkImageView view = swap.image_view(i);
        VkFramebufferCreateInfo fi{};
        fi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass      = render_pass_;
        fi.attachmentCount = 1;
        fi.pAttachments    = &view;
        fi.width           = fb_w_;
        fi.height          = fb_h_;
        fi.layers          = 1;
        if (vkCreateFramebuffer(ctx.device(), &fi, nullptr,
                                &framebuffers_[i]) != VK_SUCCESS) {
            std::fprintf(stderr, "[VkPostProcess] vkCreateFramebuffer %u failed\n", i);
        }
    }
}

void VkPostProcess::destroy_framebuffers(VkContext& ctx) {
    for (auto fb : framebuffers_) {
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(ctx.device(), fb, nullptr);
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

    if (!create_render_pass(ctx, swap.format()))              return false;
    if (!create_pipeline   (ctx, vert_spv_path, frag_spv_path)) return false;
    if (!create_samplers   (ctx))                             return false;
    if (!create_desc       (ctx))                             return false;

    create_framebuffers(ctx, swap);

    std::fprintf(stdout,
        "[VkPostProcess] Post-process pipeline ready  (%ux%u)\n",
        fb_w_, fb_h_);
    return true;
}

void VkPostProcess::rebuild(VkContext& ctx, VkSwapchain& swap) {
    if (!pipeline_) return;
    vkDeviceWaitIdle(ctx.device());
    destroy_framebuffers(ctx);
    create_framebuffers(ctx, swap);
    std::fprintf(stdout,
        "[VkPostProcess] Rebuilt framebuffers  (%ux%u)\n",
        fb_w_, fb_h_);
}

void VkPostProcess::shutdown(VkContext& ctx) {
    if (!ctx.is_ready()) return;
    vkDeviceWaitIdle(ctx.device());

    destroy_framebuffers(ctx);

    if (desc_pool_   != VK_NULL_HANDLE) { vkDestroyDescriptorPool    (ctx.device(), desc_pool_,   nullptr); desc_pool_   = VK_NULL_HANDLE; }
    if (sampler_smooth_ != VK_NULL_HANDLE) { vkDestroySampler         (ctx.device(), sampler_smooth_, nullptr); sampler_smooth_ = VK_NULL_HANDLE; }
    if (sampler_sharp_  != VK_NULL_HANDLE) { vkDestroySampler         (ctx.device(), sampler_sharp_,  nullptr); sampler_sharp_  = VK_NULL_HANDLE; }
    if (pipeline_    != VK_NULL_HANDLE) { vkDestroyPipeline           (ctx.device(), pipeline_,    nullptr); pipeline_    = VK_NULL_HANDLE; }
    if (pipe_layout_ != VK_NULL_HANDLE) { vkDestroyPipelineLayout     (ctx.device(), pipe_layout_, nullptr); pipe_layout_ = VK_NULL_HANDLE; }
    if (desc_layout_ != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(ctx.device(), desc_layout_, nullptr); desc_layout_ = VK_NULL_HANDLE; }
    if (render_pass_ != VK_NULL_HANDLE) { vkDestroyRenderPass         (ctx.device(), render_pass_, nullptr); render_pass_ = VK_NULL_HANDLE; }
    desc_smooth_ = VK_NULL_HANDLE;  // freed with the pool
    desc_sharp_  = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// VkPostProcess::apply
// ---------------------------------------------------------------------------
void VkPostProcess::apply(VkContext& ctx, VkSwapchain& swap,
                           float scr_w, float scr_h, float emu_h, float time_s,
                           const OutDestRect& dest, bool smooth,
                           float crt_strength, float scan_strength, float vignette,
                           float ntsc) {
    if (!pipeline_) return;
    if (framebuffers_.empty()) return;

    VkCommandBuffer cmd = swap.current_frame().cmd;

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
    rp_begin.framebuffer     = framebuffers_[swap.current_image_index()];
    rp_begin.renderArea      = { {0, 0}, swap.extent() };
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
        fit = aspect_fit(4, 3, swap.extent().width, swap.extent().height);
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
