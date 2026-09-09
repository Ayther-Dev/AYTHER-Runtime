// ---------------------------------------------------------------------------
// player_overlay.cpp — In-game pause overlay (M4).  See player_overlay.h.
//
// ImGui version: 1.92.8 (vcpkg).  API differences vs older versions:
//   - RenderPass and MSAASamples are now inside InitInfo.PipelineInfoMain.
//   - ImGui_ImplVulkan_CreateFontsTexture() removed; fonts upload on first frame.
//   - DescriptorPoolSize > 0 auto-creates the descriptor pool.
// ---------------------------------------------------------------------------
#include "player_overlay.h"
#include "vulkan_backend/vk_swapchain.h"
#include "vulkan_backend/vk_context.h"
#include "vulkan_backend/vk_result.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <string>

#include <ayther/ayther_session.h>  // #299: el panel opera sobre la sesión

namespace ayther {

namespace {
// AYTHER-Play-CE/ui/tokens.slint: keep the pause surface in the launcher palette.
ImVec4 play_color(int r, int g, int b, int a = 255) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

bool play_checkbox(const char* label, bool* value) {
    // Compact pause-menu row: 28px box, 12px gap, 24px label.
    // Keep ImGui's checkbox interaction, disabled state and keyboard/gamepad nav.
    ImGui::PushFont(ImGui::GetIO().FontDefault, 24.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(12, 0));
    constexpr ImGuiCol hidden_colors[] = {
        ImGuiCol_Text, ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered,
        ImGuiCol_FrameBgActive, ImGuiCol_CheckMark, ImGuiCol_Border,
        ImGuiCol_BorderShadow, ImGuiCol_CheckboxSelectedBg,
    };
    for (const auto color : hidden_colors)
        ImGui::PushStyleColor(color, ImVec4(0, 0, 0, 0));
    const bool changed = ImGui::Checkbox(label, value);
    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(2);

    const ImVec2 origin = ImGui::GetItemRectMin();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 box = origin;
    draw->AddRectFilled(box, ImVec2(box.x + 28, box.y + 28),
                        ImGui::GetColorU32(play_color(38, 38, 43)));
    draw->AddRect(ImVec2(box.x + 0.5f, box.y + 0.5f),
                  ImVec2(box.x + 27.5f, box.y + 27.5f),
                  ImGui::GetColorU32(ImGui::IsItemHovered()
                      ? play_color(250, 204, 45) : play_color(75, 75, 85)));
    if (*value) {
        // Exact check-bold outline exported from Q28ru/GrNOx (14-unit viewBox).
        // Scale the source path into the design's centered 20px icon.
        const auto point = [box](float x, float y) {
            return ImVec2(box.x + 4 + x * 20 / 14, box.y + 4 + y * 20 / 14);
        };
        ImVec2 pen(5.6875f, 10.71875f);
        const auto line = [&](float x, float y) {
            pen.x += x; pen.y += y;
            draw->PathLineTo(point(pen.x, pen.y));
        };
        const auto curve = [&](float cx, float cy, float x, float y) {
            draw->PathBezierQuadraticCurveTo(point(pen.x + cx, pen.y + cy),
                                             point(pen.x + x, pen.y + y));
            pen.x += x; pen.y += y;
        };
        draw->PathLineTo(point(pen.x, pen.y));
        curve(-0.27344f, 0, -0.4375f, -0.21875f);
        line(-3.0625f, -3.0625f);
        curve(-0.21875f, -0.16406f, -0.21875f, -0.4375f);
        curve(0, -0.27344f, 0.19141f, -0.46484f);
        curve(0.19141f, -0.19141f, 0.46484f, -0.19141f);
        curve(0.27344f, 0, 0.49219f, 0.21875f);
        line(2.57031f, 2.57031f);
        line(5.6875f, -5.6875f);
        curve(0.16406f, -0.16406f, 0.4375f, -0.16406f);
        curve(0.27344f, 0, 0.46484f, 0.19141f);
        curve(0.19141f, 0.19141f, 0.19141f, 0.46484f);
        curve(0, 0.27344f, -0.16406f, 0.49219f);
        line(-6.125f, 6.07031f);
        curve(-0.21875f, 0.21875f, -0.49219f, 0.21875f);
        draw->PathFillConcave(ImGui::GetColorU32(play_color(250, 204, 45)));
    }
    draw->AddText(ImVec2(origin.x + 40, origin.y + 2),
                  ImGui::GetColorU32(play_color(214, 214, 220)), label);
    ImGui::PopFont();
    return changed;
}

ImFont* load_play_font(ImGuiIO& io, const char* filename, float size) {
    const char* base = SDL_GetBasePath();
    if (!base) return nullptr;
    const std::string path = std::string(base) + "fonts/" + filename;
    size_t length = 0;
    void* data = SDL_LoadFile(path.c_str(), &length);
    if (!data) return nullptr;
    // Transfer a copy to the atlas using its allocator (SDL owns the original).
    void* owned = ImGui::MemAlloc(length);
    std::memcpy(owned, data, length);
    SDL_free(data);
    return io.Fonts->AddFontFromMemoryTTF(owned, static_cast<int>(length), size);
}

struct VolumeEdit {
    bool mute_changed;
    bool gain_changed;
};

VolumeEdit play_volume(const char* label, float& gain, bool& muted,
                       ImFont* value_font, ImFont* label_font) {
    // Updated I5GINs: full-width transparent row, inline Inter label, 12px gaps.
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const auto point = [origin](float x, float y) {
        return ImVec2(origin.x + x, origin.y + y);
    };
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 background = ImGui::GetColorU32(play_color(38, 38, 43));
    const ImU32 border = ImGui::GetColorU32(play_color(75, 75, 85));
    const ImU32 accent = ImGui::GetColorU32(play_color(250, 204, 45));

    ImGui::PushFont(label_font ? label_font : ImGui::GetIO().FontDefault, 16);
    // Use one label column so tracks line up across the three audio buses.
    const float label_width = std::max({ImGui::CalcTextSize("Música").x,
        ImGui::CalcTextSize("Efectos").x, ImGui::CalcTextSize("Voces").x});
    draw->AddText(point(16, 16), ImGui::GetColorU32(play_color(255, 255, 255)), label);
    ImGui::PopFont();
    const float icon_start = 16 + label_width + 12;

    ImGui::BeginGroup();
    ImGui::SetCursorScreenPos(point(icon_start - 4, 0));
    const bool mute_changed = ImGui::InvisibleButton(
        "##mute", ImVec2(32, 48), ImGuiButtonFlags_EnableNav);
    if (mute_changed) muted = !muted;
    if (ImGui::IsItemHovered() || ImGui::IsItemFocused())
        ImGui::SetTooltip(muted ? "Activar sonido" : "Silenciar");

    // Lucide volume-2 outline, in its original 24px coordinate system.
    const auto icon = [&](float x, float y) { return point(icon_start + x, 12 + y); };
    const ImU32 ink = ImGui::GetColorU32(muted
        ? play_color(165, 165, 174) : play_color(214, 214, 220));
    draw->PathLineTo(icon(11, 4.702f));
    draw->PathBezierQuadraticCurveTo(icon(11, 3.705f), icon(9.797f, 4.204f));
    draw->PathLineTo(icon(6.413f, 7.587f));
    draw->PathBezierQuadraticCurveTo(icon(6, 8), icon(5.416f, 8));
    draw->PathLineTo(icon(3, 8));
    draw->PathBezierQuadraticCurveTo(icon(2, 8), icon(2, 9));
    draw->PathLineTo(icon(2, 15));
    draw->PathBezierQuadraticCurveTo(icon(2, 16), icon(3, 16));
    draw->PathLineTo(icon(5.416f, 16));
    draw->PathBezierQuadraticCurveTo(icon(6, 16), icon(6.413f, 16.413f));
    draw->PathLineTo(icon(9.797f, 19.797f));
    draw->PathBezierQuadraticCurveTo(icon(11, 20.295f), icon(11, 19.298f));
    draw->PathStroke(ink, 2.0f, ImDrawFlags_Closed);
    if (muted) {
        draw->AddLine(icon(16, 9), icon(22, 15), ink, 2);
        draw->AddLine(icon(22, 9), icon(16, 15), ink, 2);
    } else {
        draw->PathArcTo(icon(12, 12), 5, -0.6435011f, 0.6435011f);
        draw->PathStroke(ink, 2.0f);
        draw->PathArcTo(icon(13, 12), 9, -0.7853982f, 0.7853982f);
        draw->PathStroke(ink, 2.0f);
        for (const ImVec2 end : {icon(16, 9), icon(16, 15),
                                 icon(19.364f, 5.636f), icon(19.364f, 18.364f)})
            draw->AddCircleFilled(end, 1, ink);
    }

    const float track_start = icon_start + 24 + 12;
    // Reserve four characters for the existing 0–200% gain range.
    ImGui::PushFont(value_font ? value_font : ImGui::GetIO().FontDefault, 16);
    const float value_width = ImGui::CalcTextSize("200%").x;
    ImGui::PopFont();
    const float value_start = width - 16 - value_width;
    const float track_end = std::max(track_start + 1, value_start - 12);
    // ImGui reserves 2px padding + half the 14px grab at either end.
    // Compensate so the hit positions agree with the visible track endpoints.
    ImGui::SetCursorScreenPos(point(track_start - 9, 0));
    ImGui::SetNextItemWidth(track_end - track_start + 18);
    ImGui::PushFont(ImGui::GetIO().FontDefault, 18);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 15));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 14);
    constexpr ImGuiCol hidden[] = {
        ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
        ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_Border,
        ImGuiCol_BorderShadow, ImGuiCol_Text,
    };
    for (const auto color : hidden)
        ImGui::PushStyleColor(color, ImVec4(0, 0, 0, 0));
    const bool gain_changed = ImGui::SliderFloat(
        "##gain", &gain, 0, 2, "%.2f", ImGuiSliderFlags_NoInput);
    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    const float thumb = track_start + (track_end - track_start)
        * std::clamp(gain / 2, 0.0f, 1.0f);
    draw->AddRectFilled(point(track_start, 22.5f), point(track_end, 25.5f), border);
    draw->AddRectFilled(point(track_start, 22), point(thumb, 26), accent, 2);
    draw->AddCircleFilled(point(thumb, 24), 9, background);
    draw->AddCircleFilled(point(thumb, 24), 7, accent);
    char percent[16];
    std::snprintf(percent, sizeof(percent), "%.0f%%", gain * 100);
    ImGui::PushFont(value_font ? value_font : ImGui::GetIO().FontDefault, 16);
    draw->AddText(point(value_start, 16),
                  ImGui::GetColorU32(play_color(240, 240, 245)), percent);
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, 48));
    ImGui::EndGroup();
    return {mute_changed, gain_changed};
}
} // namespace

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void barrier_to_color(VkCommandBuffer cmd, VkImage image) {
    VkImageMemoryBarrier b{};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);
}

// ---------------------------------------------------------------------------
// Render pass — same pattern as VkSprite (loadOp=LOAD, TRANSFER_DST exit)
// ---------------------------------------------------------------------------
bool PlayerOverlay::create_render_pass(VkContext& ctx, VkFormat fmt) {
    VkAttachmentDescription att{};
    att.format         = fmt;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;        // keep game frame
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;  // after explicit barrier
    att.finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;      // for finalize()

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_ref;

    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass    = 0;
    deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    VkRenderPassCreateInfo rp_info{};
    rp_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.attachmentCount = 1;
    rp_info.pAttachments    = &att;
    rp_info.subpassCount    = 1;
    rp_info.pSubpasses      = &subpass;
    rp_info.dependencyCount = 2;
    rp_info.pDependencies   = deps;

    return ayther::runtime::vulkan::require_vk_success(
        "vkCreateRenderPass [PlayerOverlay]",
        ctx.calls().create_render_pass(
            ctx.device(), &rp_info, nullptr, &render_pass_));
}

// ---------------------------------------------------------------------------
// Framebuffers — one per swapchain image, over the swapchain image views
// ---------------------------------------------------------------------------
bool PlayerOverlay::create_framebuffers(
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
                "vkCreateFramebuffer [PlayerOverlay]",
                ctx.calls().create_framebuffer(
                    ctx.device(), &fi, nullptr, &output[i]))) {
            return false;
        }
    }
    return true;
}

void PlayerOverlay::destroy_framebuffers(VkContext& ctx) {
    for (auto fb : framebuffers_)
        if (fb != VK_NULL_HANDLE)
            ctx.calls().destroy_framebuffer(ctx.device(), fb, nullptr);
    framebuffers_.clear();
    fb_w_ = fb_h_ = 0;
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
bool PlayerOverlay::init(VkContext& ctx, VkSwapchain& swap, SDL_Window* window) {
    if (!ctx.is_ready() || !swap.is_ready() || window == nullptr) return false;
    if (is_ready()) return true;

    if (!create_render_pass(ctx, swap.format()) ||
        !create_framebuffers(ctx, swap, framebuffers_)) {
        shutdown(ctx);
        return false;
    }
    fb_w_ = swap.extent().width;
    fb_h_ = swap.extent().height;

    // ---- ImGui core context (independent — multi-context safe) ---------------
    overlay_ctx_ = ImGui::CreateContext();
    if (overlay_ctx_ == nullptr) {
        shutdown(ctx);
        return false;
    }
    ImGui::SetCurrentContext(overlay_ctx_);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename  = nullptr;   // no persistent imgui.ini in the game session

    io.FontDefault = load_play_font(io, "IBMPlexSans-Regular.ttf", 18.0f);
    if (!io.FontDefault) io.FontDefault = io.Fonts->AddFontDefault();
    title_font_ = load_play_font(io, "Khand-SemiBold.ttf", 32.0f);
    volume_font_ = load_play_font(io, "IBMPlexMono-SemiBold.ttf", 16.0f);
    volume_label_font_ = load_play_font(io, "Inter.ttf", 16.0f);

    // Graphite surfaces, square controls and the Play CE yellow accent.
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowPadding = ImVec2(24, 20);
    style.FramePadding = ImVec2(12, 6);
    style.ItemSpacing = ImVec2(12, 8);
    style.Colors[ImGuiCol_WindowBg] = play_color(42, 42, 47);
    style.Colors[ImGuiCol_PopupBg] = play_color(50, 50, 56);
    style.Colors[ImGuiCol_Text] = play_color(214, 214, 220);
    style.Colors[ImGuiCol_TextDisabled] = play_color(165, 165, 174);
    style.Colors[ImGuiCol_Border] = play_color(75, 75, 85);
    style.Colors[ImGuiCol_Separator] = play_color(75, 75, 85);
    style.Colors[ImGuiCol_FrameBg] = play_color(38, 38, 43);
    style.Colors[ImGuiCol_FrameBgHovered] = play_color(72, 68, 44);
    style.Colors[ImGuiCol_FrameBgActive] = play_color(94, 83, 38);
    style.Colors[ImGuiCol_Button] = play_color(59, 59, 66);
    style.Colors[ImGuiCol_ButtonHovered] = play_color(72, 68, 44);
    style.Colors[ImGuiCol_ButtonActive] = play_color(94, 83, 38);
    style.Colors[ImGuiCol_CheckMark] = play_color(250, 204, 45);
    style.Colors[ImGuiCol_SliderGrab] = play_color(250, 204, 45);
    style.Colors[ImGuiCol_SliderGrabActive] = play_color(252, 221, 111);
    style.Colors[ImGuiCol_Header] = play_color(250, 204, 45, 26);
    style.Colors[ImGuiCol_HeaderHovered] = play_color(250, 204, 45, 46);
    style.Colors[ImGuiCol_HeaderActive] = play_color(250, 204, 45, 82);
    style.Colors[ImGuiCol_NavHighlight] = play_color(250, 204, 45);
    style.Colors[ImGuiCol_TextSelectedBg] = play_color(250, 204, 45, 82);

    // ---- SDL3 backend --------------------------------------------------------
    if (!ImGui_ImplSDL3_InitForVulkan(window)) {
        ImGui::DestroyContext(overlay_ctx_);
        overlay_ctx_ = nullptr;
        shutdown(ctx);
        return false;
    }

    // ---- Vulkan backend (ImGui 1.92.8 API) -----------------------------------
    // RenderPass and MSAASamples moved to InitInfo.PipelineInfoMain since 1.92.
    // DescriptorPoolSize > 0 auto-creates the internal descriptor pool.
    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance                       = ctx.instance();
    init_info.PhysicalDevice                 = ctx.physical_device();
    init_info.Device                         = ctx.device();
    init_info.QueueFamily                    = ctx.graphics_family();
    init_info.Queue                          = ctx.graphics_queue();
    init_info.DescriptorPool                 = VK_NULL_HANDLE;  // auto via DescriptorPoolSize
    init_info.DescriptorPoolSize             = 2;               // font atlas + 1 spare
    init_info.MinImageCount                  = VkSwapchain::kMaxFrames;
    init_info.ImageCount                     = swap.image_count();
    init_info.PipelineInfoMain.RenderPass    = render_pass_;
    init_info.PipelineInfoMain.MSAASamples   = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        std::fprintf(stderr, "[PlayerOverlay] ImGui_ImplVulkan_Init failed\n");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(overlay_ctx_);
        overlay_ctx_ = nullptr;
        shutdown(ctx);
        return false;
    }
    // Fonts upload automatically on the first ImGui_ImplVulkan_NewFrame() call
    // (ImGui 1.88+ — no explicit CreateFontsTexture() needed).

    imgui_ready_ = true;
    std::fprintf(stdout, "[PlayerOverlay] ready  (%ux%u)  Escape/Guide=pause  F1=toggle HD\n",
                 fb_w_, fb_h_);
    return true;
}

// ---------------------------------------------------------------------------
// rebuild — call after swapchain resize
// ---------------------------------------------------------------------------
bool PlayerOverlay::rebuild(VkContext& ctx, VkSwapchain& swap) {
    if (!render_pass_ || !swap.is_ready()) return false;
    if (const auto failure =
            ctx.wait_idle("vkDeviceWaitIdle [PlayerOverlay::rebuild]")) {
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
    std::fprintf(stdout, "[PlayerOverlay] rebuilt (%ux%u)\n", fb_w_, fb_h_);
    return true;
}

// ---------------------------------------------------------------------------
// handle_event
// ---------------------------------------------------------------------------
void PlayerOverlay::handle_event(const SDL_Event& e) {
    if (!imgui_ready_) return;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(overlay_ctx_);
    ImGui_ImplSDL3_ProcessEvent(&e);
    ImGui::SetCurrentContext(prev);
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------
namespace {

/// Los subsistemas VISUALES agrupados como el jugador los nombra. Un jugador no
/// sabe qué es un «metasprite» ni por qué «tiles» y «planes» son dos cosas: lo
/// que ve es que los personajes, el escenario y la interfaz cambian o no.
///
/// Cada entrada enciende varios `Subsystem` a la vez, y por eso la fila está
/// disponible si CUALQUIERA de los suyos lo está: si el pack trae sprites pero
/// no metasprites, «Personajes» sigue haciendo algo.
struct VisualGroup {
    const char* label;
    ayther::Subsystem members[3];
    int          count;
};
constexpr VisualGroup kVisual[] = {
    { "Personajes", { ayther::Subsystem::Sprites, ayther::Subsystem::Metasprites }, 2 },
    { "Escenario",  { ayther::Subsystem::Tiles,   ayther::Subsystem::Planes      }, 2 },
    { "Interfaz",   { ayther::Subsystem::Ui },                                      1 },
};

}  // namespace

void PlayerOverlay::render(VkContext&, const AcquiredFrame& frame,
                            bool& hd_on, bool& running,
                            AytherSession* session, PlayerConfig* cfg,
                            bool* shaders_on) {
    const auto framebuffer = frame.framebuffer(framebuffers_);
    if (!paused_ || !imgui_ready_ || !framebuffer) return;
    const VkCommandBuffer cmd = frame.command_buffer();

    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(overlay_ctx_);

    // ---- ImGui frame --------------------------------------------------------
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // Use logical viewport coordinates, including on high-DPI displays.
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        vp->Pos,
        ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
        IM_COL32(20, 20, 23, 158));

    // ---- Pause menu ---------------------------------------------------------
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    const float width = std::min(480.0f, std::max(1.0f, vp->Size.x - 24.0f));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(width, 0), ImVec2(width, std::max(1.0f, vp->Size.y - 24.0f)));
    ImGui::SetNextWindowSize(ImVec2(width, 0),
                             ImGuiCond_Always);

    constexpr ImGuiWindowFlags kWinFlags =
        ImGuiWindowFlags_NoTitleBar      |
        ImGuiWindowFlags_NoResize        |
        ImGuiWindowFlags_NoCollapse      |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin("##pause_overlay", nullptr, kWinFlags);

    ImGui::TextColored(play_color(250, 204, 45), "AYTHER PLAY CE");
    ImGui::PushFont(title_font_ ? title_font_ : ImGui::GetIO().FontDefault, 32.0f);
    ImGui::TextColored(play_color(240, 240, 245), "EN PAUSA");
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Primary action uses the same yellow fill and dark text as Play CE.
    ImGui::PushStyleColor(ImGuiCol_Text, play_color(27, 26, 16));
    ImGui::PushStyleColor(ImGuiCol_Button, play_color(250, 204, 45));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, play_color(252, 221, 111));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, play_color(201, 160, 6));
    if (ImGui::Button("Continuar", ImVec2(-1, 40)))
        paused_ = false;
    ImGui::PopStyleColor(4);

    ImGui::Spacing();

    // HD toggle
    const char* hd_label = hd_on ? "Modo HD: activado" : "Modo HD: desactivado";
    if (ImGui::Button(hd_label, ImVec2(-1, 0))) {
        hd_on = !hd_on;
        std::fprintf(stdout, "[overlay] HD mode: %s\n", hd_on ? "ON" : "OFF");
    }

    // ---- #301: degradación segura -------------------------------------------
    //
    // El mensaje va ACÁ y no como un aviso flotante: el fallback es silencioso
    // por diseño —se oye el juego original y listo— y un cartel por frame
    // convertiría un pack con un asset roto en una molestia. Acá lo ve quien
    // abrió el menú justamente porque notó que algo no sonaba.
    //
    // Y lo redacta el Engine: el que sabe QUÉ pasó es el que lo contó.
    if (session) {
        const std::string msg = session->degradation_message();
        if (!msg.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.15f, 1.0f));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(msg.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            if (ImGui::Button("  Volver a intentar  ", ImVec2(-1, 0))) {
                // El motor no reintenta solo: volver a probar lo mismo que ya
                // falló doce veces es lo que la escalada existe para no hacer.
                // Que lo pida el usuario es lo que lo hace una decisión.
                session->clear_auto_disabled();
                cfg_dirty_ = true;
            }
            ImGui::Spacing();
            ImGui::Separator();
        }
    }

    // ---- #299: configuración de AYTHER --------------------------------------
    //
    // Todo se aplica EN EL ACTO sobre la sesión: es el criterio de la issue y
    // además es lo único que tiene sentido acá — el jugador está mirando el
    // frame que va a cambiar.
    //
    // Lo que el pack no trae aparece DESACTIVADO y no oculto: un control que
    // desaparece se lee como «esta versión no lo tiene»; uno gris dice «este
    // pack no lo trae», que es la verdad (`SubsystemAvailability`, #292).
    if (session && cfg) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // -- Perfil ----------------------------------------------------------
        const uint32_t np = session->profile_count();
        if (np) {
            const std::string act = session->active_profile();
            // Vacío = el jugador tocó algo suelto y el estado ya no es ningún
            // perfil. Se dice, en vez de mostrar el último elegido: seguir
            // marcando «Mejorado» mentiría sobre lo que está viendo.
            const std::string shown = act.empty() ? std::string("personalizado") : act;
            if (ImGui::BeginCombo("Perfil", shown.c_str())) {
                for (uint32_t i = 0; i < np; ++i) {
                    const std::string id = session->profile_id(i);
                    const std::string nm = session->profile_name(i);
                    if (ImGui::Selectable((nm.empty() ? id : nm).c_str(), id == act)) {
                        if (session->set_profile(id)) {
                            cfg->profile         = id;
                            cfg->subsystems      = session->subsystems_enabled_mask();
                            cfg->have_subsystems = true;
                            cfg_dirty_ = true;
                        }
                    }
                }
                ImGui::EndCombo();
            }
        }

        // -- Qué se sustituye ------------------------------------------------
        ImGui::Spacing();
        ImGui::TextColored(play_color(165, 165, 174), "MEJORAS VISUALES");
        for (const auto& g : kVisual) {
            bool any_present = false, any_on = false;
            for (int i = 0; i < g.count; ++i) {
                if (session->subsystem_availability(g.members[i])
                    != SubsystemAvailability::Absent) any_present = true;
                if (session->subsystem_enabled(g.members[i])) any_on = true;
            }
            ImGui::BeginDisabled(!any_present);
            bool v = any_on;
            if (play_checkbox(g.label, &v)) {
                for (int i = 0; i < g.count; ++i)
                    session->set_subsystem_enabled(g.members[i], v);
                cfg->subsystems      = session->subsystems_enabled_mask();
                cfg->have_subsystems = true;
                cfg_dirty_ = true;
            }
            ImGui::EndDisabled();
            if (!any_present && ImGui::IsItemHovered())
                ImGui::SetTooltip("Este pack no lo trae");
        }

        // -- Audio ------------------------------------------------------------
        ImGui::Spacing();
        ImGui::TextColored(play_color(165, 165, 174), "AUDIO");
        struct BusRow { const char* label; AudioBus bus; Subsystem sub; };
        const BusRow kBuses[] = {
            { "Música",  AudioBus::Music, Subsystem::Music },
            { "Efectos", AudioBus::Sfx,   Subsystem::Sfx   },
            { "Voces",   AudioBus::Voice, Subsystem::Sfx   },
        };
        for (const auto& b : kBuses) {
            ImGui::PushID(b.label);
            bool muted = session->bus_muted(b.bus);
            float g = session->bus_volume(b.bus);
            const auto edit = play_volume(b.label, g, muted, volume_font_, volume_label_font_);
            if (edit.mute_changed) {
                session->set_bus_muted(b.bus, muted);
                cfg->bus_muted[static_cast<int>(b.bus)] = muted;
                cfg_dirty_ = true;
            }
            if (edit.gain_changed) {
                session->set_bus_volume(b.bus, g);
                cfg->bus_gain[static_cast<int>(b.bus)] = g;
                cfg_dirty_ = true;
            }
            ImGui::PopID();
        }

        // -- Shaders -----------------------------------------------------------
        if (shaders_on) {
            ImGui::Spacing();
            bool sh = *shaders_on;
            if (play_checkbox("Shaders de presentación", &sh)) {
                *shaders_on     = sh;
                cfg->shaders_on = sh;
                cfg_dirty_      = true;
            }
        }

        // -- Restaurar ---------------------------------------------------------
        ImGui::Spacing();
        if (ImGui::Button("  Restaurar valores del pack  ", ImVec2(-1, 0))) {
            // «Predeterminado» es el del PACK, no el de fábrica: lo que el
            // autor eligió como su forma de mostrarse. Volver a «todo
            // encendido» sería restaurar algo que nadie decidió.
            session->apply_default_profile();
            for (uint32_t i = 0; i < kAudioBusCount; ++i) {
                session->set_bus_volume(static_cast<AudioBus>(i), 1.0f);
                session->set_bus_muted (static_cast<AudioBus>(i), false);
            }
            *cfg = PlayerConfig{};
            cfg->profile         = session->active_profile();
            cfg->subsystems      = session->subsystems_enabled_mask();
            cfg->have_subsystems = true;
            if (shaders_on) *shaders_on = true;
            hd_on      = true;
            cfg_dirty_ = true;
        }
        ImGui::Spacing();
        ImGui::Separator();
    }

    ImGui::Spacing();

    // Returning to the library is a secondary navigation action.
    if (ImGui::Button("Volver al launcher", ImVec2(-1, 40)))
        running = false;

    ImGui::Spacing();
    ImGui::End();

    ImGui::Render();

    // ---- Vulkan render pass -------------------------------------------------
    // TRANSFER_DST_OPTIMAL → COLOR_ATTACHMENT_OPTIMAL (explicit barrier)
    barrier_to_color(cmd, frame.image());

    VkClearValue clear_val{};
    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass      = render_pass_;
    rp_begin.framebuffer     = *framebuffer;
    rp_begin.renderArea      = { {0, 0}, { fb_w_, fb_h_ } };
    rp_begin.clearValueCount = 1;
    rp_begin.pClearValues    = &clear_val;

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);
    // finalLayout = TRANSFER_DST_OPTIMAL (auto-transition) — VkPresent::finalize() unchanged

    ImGui::SetCurrentContext(prev);
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------
void PlayerOverlay::shutdown(VkContext& ctx) {
    if (!ctx.is_ready()) return;
    if (const auto failure =
            ctx.wait_idle("vkDeviceWaitIdle [PlayerOverlay::shutdown]")) {
        ayther::runtime::vulkan::log_vk_failure(*failure);
    }

    if (imgui_ready_) {
        ImGui::SetCurrentContext(overlay_ctx_);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(overlay_ctx_);
        overlay_ctx_ = nullptr;
        imgui_ready_ = false;
    }

    destroy_framebuffers(ctx);
    if (render_pass_ != VK_NULL_HANDLE) {
        ctx.calls().destroy_render_pass(ctx.device(), render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
}

}  // namespace ayther
