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

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <SDL3/SDL.h>
#include <cstdio>
#include <string>

#include "ayther_session.h"   // #299: el panel opera sobre la sesión

namespace ayther {

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

    if (vkCreateRenderPass(ctx.device(), &rp_info, nullptr, &render_pass_) != VK_SUCCESS) {
        std::fprintf(stderr, "[PlayerOverlay] vkCreateRenderPass failed\n");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Framebuffers — one per swapchain image, over the swapchain image views
// ---------------------------------------------------------------------------
void PlayerOverlay::create_framebuffers(VkContext& ctx, VkSwapchain& swap) {
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
        if (vkCreateFramebuffer(ctx.device(), &fi, nullptr, &framebuffers_[i]) != VK_SUCCESS)
            std::fprintf(stderr, "[PlayerOverlay] vkCreateFramebuffer %u failed\n", i);
    }
}

void PlayerOverlay::destroy_framebuffers(VkContext& ctx) {
    for (auto fb : framebuffers_)
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(ctx.device(), fb, nullptr);
    framebuffers_.clear();
    fb_w_ = fb_h_ = 0;
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
bool PlayerOverlay::init(VkContext& ctx, VkSwapchain& swap, SDL_Window* window) {
    if (!ctx.is_ready() || !swap.is_ready()) return false;

    if (!create_render_pass(ctx, swap.format())) return false;
    create_framebuffers(ctx, swap);

    // ---- ImGui core context (independent — multi-context safe) ---------------
    overlay_ctx_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(overlay_ctx_);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename  = nullptr;   // no persistent imgui.ini in the game session

    // Dark style with amber hover/active (matches Ayther brand).
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 8.0f;
    style.FrameRounding     = 5.0f;
    style.GrabRounding      = 5.0f;
    style.WindowBorderSize  = 0.0f;
    style.WindowPadding     = ImVec2(18, 14);
    style.ItemSpacing       = ImVec2(10, 8);
    style.Colors[ImGuiCol_WindowBg]      = ImVec4(0.08f, 0.08f, 0.10f, 0.92f);
    style.Colors[ImGuiCol_Button]        = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.85f, 0.55f, 0.05f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive]  = ImVec4(1.00f, 0.70f, 0.10f, 1.0f);
    style.Colors[ImGuiCol_NavHighlight]  = ImVec4(0.85f, 0.55f, 0.05f, 1.0f);

    // ---- SDL3 backend --------------------------------------------------------
    ImGui_ImplSDL3_InitForVulkan(window);

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
        ImGui::DestroyContext(overlay_ctx_);
        overlay_ctx_ = nullptr;
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
void PlayerOverlay::rebuild(VkContext& ctx, VkSwapchain& swap) {
    if (!render_pass_) return;
    vkDeviceWaitIdle(ctx.device());
    destroy_framebuffers(ctx);
    create_framebuffers(ctx, swap);
    std::fprintf(stdout, "[PlayerOverlay] rebuilt (%ux%u)\n", fb_w_, fb_h_);
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

void PlayerOverlay::render(VkContext& ctx, VkCommandBuffer cmd, VkSwapchain& swap,
                            bool& hd_on, bool& running,
                            AytherSession* session, PlayerConfig* cfg,
                            bool* shaders_on) {
    if (!paused_ || !imgui_ready_ || framebuffers_.empty()) return;

    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(overlay_ctx_);

    // ---- ImGui frame --------------------------------------------------------
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Semi-transparent dim over the entire game frame.
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0, 0),
        ImVec2(static_cast<float>(fb_w_), static_cast<float>(fb_h_)),
        IM_COL32(0, 0, 0, 140));

    // ---- Pause menu ---------------------------------------------------------
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    // #299: el panel de configuración necesita más ancho que el menú de pausa
    // pelado, y los sliders de bus no entran en 280.
    ImGui::SetNextWindowSize(ImVec2((session && cfg) ? 380.0f : 280.0f, 0),
                             ImGuiCond_Always);

    constexpr ImGuiWindowFlags kWinFlags =
        ImGuiWindowFlags_NoDecoration    |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::Begin("##pause_overlay", nullptr, kWinFlags);

    // Title (centered)
    const char* kTitle = "PAUSED";
    ImGui::SetCursorPosX(
        (ImGui::GetWindowWidth() - ImGui::CalcTextSize(kTitle).x) * 0.5f);
    ImGui::TextUnformatted(kTitle);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Resume (auto-focused on open — NavHighlight picks it up)
    if (ImGui::Button("  Resume  ", ImVec2(-1, 0)))
        paused_ = false;

    ImGui::Spacing();

    // HD toggle
    const char* hd_label = hd_on ? "  HD Mode: ON   " : "  HD Mode: OFF  ";
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
        ImGui::TextUnformatted("Sustituir");
        for (const auto& g : kVisual) {
            bool any_present = false, any_on = false;
            for (int i = 0; i < g.count; ++i) {
                if (session->subsystem_availability(g.members[i])
                    != SubsystemAvailability::Absent) any_present = true;
                if (session->subsystem_enabled(g.members[i])) any_on = true;
            }
            ImGui::BeginDisabled(!any_present);
            bool v = any_on;
            if (ImGui::Checkbox(g.label, &v)) {
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
        ImGui::TextUnformatted("Audio");
        struct BusRow { const char* label; AudioBus bus; Subsystem sub; };
        const BusRow kBuses[] = {
            { "Música",  AudioBus::Music, Subsystem::Music },
            { "Efectos", AudioBus::Sfx,   Subsystem::Sfx   },
            { "Voces",   AudioBus::Voice, Subsystem::Sfx   },
        };
        for (const auto& b : kBuses) {
            ImGui::PushID(b.label);
            bool m = session->bus_muted(b.bus);
            if (ImGui::Checkbox("##mute", &m)) {
                // La casilla dice «suena», no «silenciado»: el jugador razona
                // en positivo y una casilla marcada que significa «apagado» se
                // lee al revés la mitad de las veces.
                session->set_bus_muted(b.bus, m);
                cfg->bus_muted[static_cast<int>(b.bus)] = m;
                cfg_dirty_ = true;
            }
            ImGui::SameLine();
            float g = session->bus_volume(b.bus);
            ImGui::SetNextItemWidth(150);
            if (ImGui::SliderFloat(b.label, &g, 0.0f, 2.0f, "%.2f")) {
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
            if (ImGui::Checkbox("Shaders de presentación", &sh)) {
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

    // Quit (red tint)
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.15f, 0.15f, 1.0f));
    if (ImGui::Button("  Quit to Launcher  ", ImVec2(-1, 0)))
        running = false;
    ImGui::PopStyleColor(2);

    ImGui::Spacing();
    ImGui::End();

    ImGui::Render();

    // ---- Vulkan render pass -------------------------------------------------
    // TRANSFER_DST_OPTIMAL → COLOR_ATTACHMENT_OPTIMAL (explicit barrier)
    barrier_to_color(cmd, swap.current_image());

    VkClearValue clear_val{};
    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass      = render_pass_;
    rp_begin.framebuffer     = framebuffers_[swap.current_image_index()];
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
    vkDeviceWaitIdle(ctx.device());

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
        vkDestroyRenderPass(ctx.device(), render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
}

}  // namespace ayther
