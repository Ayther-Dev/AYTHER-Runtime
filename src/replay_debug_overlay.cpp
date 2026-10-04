#include "replay_debug_overlay.h"

#include "vulkan_backend/vk_context.h"
#include "vulkan_backend/vk_result.h"
#include "vulkan_backend/vk_swapchain.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <utility>

namespace ayther::runtime {
namespace {

void barrier_to_color(VkCommandBuffer command, VkImage image) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);
}

// The ImGui context of the overlay is restored to the caller's on every exit.
class ContextScope final {
  public:
    explicit ContextScope(ImGuiContext *context) : previous_(ImGui::GetCurrentContext()) {
        ImGui::SetCurrentContext(context);
    }
    ~ContextScope() { ImGui::SetCurrentContext(previous_); }
    ContextScope(const ContextScope &) = delete;
    ContextScope &operator=(const ContextScope &) = delete;

  private:
    ImGuiContext *previous_;
};

} // namespace

bool ReplayDebugOverlay::create_render_pass(VkContext &context, VkFormat format) {
    // Same pattern as the player overlay: load the game frame, leave it for finalize().
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dependencies[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &attachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = dependencies;
    return vulkan::require_vk_success(
        "vkCreateRenderPass [ReplayDebugOverlay]",
        context.calls().create_render_pass(context.device(), &info, nullptr, &render_pass_));
}

bool ReplayDebugOverlay::create_framebuffers(VkContext &context, VkSwapchain &swapchain,
                                             std::vector<VkFramebuffer> &output) const {
    const auto views = swapchain.image_views();
    output.assign(views.size(), VK_NULL_HANDLE);
    for (std::size_t index = 0; index < views.size(); ++index) {
        const VkImageView view = views[index];
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = render_pass_;
        info.attachmentCount = 1;
        info.pAttachments = &view;
        info.width = swapchain.extent().width;
        info.height = swapchain.extent().height;
        info.layers = 1;
        if (!vulkan::require_vk_success("vkCreateFramebuffer [ReplayDebugOverlay]",
                                        context.calls().create_framebuffer(
                                            context.device(), &info, nullptr, &output[index])))
            return false;
    }
    return true;
}

void ReplayDebugOverlay::destroy_framebuffers(VkContext &context) {
    for (auto framebuffer : framebuffers_)
        if (framebuffer != VK_NULL_HANDLE)
            context.calls().destroy_framebuffer(context.device(), framebuffer, nullptr);
    framebuffers_.clear();
}

bool ReplayDebugOverlay::init(VkContext &context, VkSwapchain &swapchain, SDL_Window *window) {
    if (!context.is_ready() || !swapchain.is_ready() || window == nullptr)
        return false;
    if (imgui_ready_)
        return true;
    if (!create_render_pass(context, swapchain.format()) ||
        !create_framebuffers(context, swapchain, framebuffers_)) {
        shutdown(context);
        return false;
    }
    width_ = swapchain.extent().width;
    height_ = swapchain.extent().height;
    context_ = ImGui::CreateContext();
    if (context_ == nullptr) {
        shutdown(context);
        return false;
    }
    const ContextScope scope{context_};
    auto &io = ImGui::GetIO();
    // D15: no keyboard navigation; the overlay never takes Space, the arrows or I.
    io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL3_InitForVulkan(window)) {
        ImGui::DestroyContext(context_);
        context_ = nullptr;
        shutdown(context);
        return false;
    }
    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = context.instance();
    info.PhysicalDevice = context.physical_device();
    info.Device = context.device();
    info.QueueFamily = context.graphics_family();
    info.Queue = context.graphics_queue();
    info.DescriptorPoolSize = 2;
    info.MinImageCount = VkSwapchain::kMaxFrames;
    info.ImageCount = swapchain.image_count();
    info.PipelineInfoMain.RenderPass = render_pass_;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    if (!ImGui_ImplVulkan_Init(&info)) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(context_);
        context_ = nullptr;
        shutdown(context);
        return false;
    }
    imgui_ready_ = true;
    return true;
}

bool ReplayDebugOverlay::rebuild(VkContext &context, VkSwapchain &swapchain) {
    if (render_pass_ == VK_NULL_HANDLE || !swapchain.is_ready())
        return false;
    std::vector<VkFramebuffer> pending;
    if (!create_framebuffers(context, swapchain, pending)) {
        for (const auto framebuffer : pending)
            if (framebuffer != VK_NULL_HANDLE)
                context.calls().destroy_framebuffer(context.device(), framebuffer, nullptr);
        return false;
    }
    destroy_framebuffers(context);
    framebuffers_ = std::move(pending);
    width_ = swapchain.extent().width;
    height_ = swapchain.extent().height;
    return true;
}

void ReplayDebugOverlay::shutdown(VkContext &context) {
    if (!context.is_ready())
        return;
    if (const auto failure = context.wait_idle("ReplayDebugOverlay shutdown"))
        vulkan::log_vk_failure(*failure);
    if (imgui_ready_) {
        const ContextScope scope{context_};
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
    }
    if (context_ != nullptr) {
        ImGui::DestroyContext(context_);
        context_ = nullptr;
    }
    imgui_ready_ = false;
    destroy_framebuffers(context);
    if (render_pass_ != VK_NULL_HANDLE) {
        context.calls().destroy_render_pass(context.device(), render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
}

void ReplayDebugOverlay::scroll(replay_inspection::ScrollKey key,
                                std::size_t total_lines) noexcept {
    scroll_.apply(key, visible_lines_, total_lines);
}

void ReplayDebugOverlay::render(const AcquiredFrame &frame, bool visible,
                                const std::vector<replay_inspection::DebugLine> &lines,
                                const std::string &notice) {
    const auto framebuffer = frame.framebuffer(framebuffers_);
    if (!imgui_ready_ || !framebuffer || (!visible && notice.empty()))
        return;
    const ContextScope scope{context_};
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const auto *viewport = ImGui::GetMainViewport();
    if (!notice.empty()) {
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x * 0.5F, viewport->Pos.y + 24.0F),
            ImGuiCond_Always, ImVec2(0.5F, 0.0F));
        ImGui::Begin("##notice", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
        ImGui::TextUnformatted(notice.c_str());
        ImGui::End();
    }
    if (visible) {
        // RF-7: the record of the frame, scrolled by the overlay, never cut horizontally.
        const float line_height = ImGui::GetTextLineHeightWithSpacing();
        const float height = viewport->Size.y * 0.6F;
        visible_lines_ = std::max<std::size_t>(1U, static_cast<std::size_t>(height / line_height));
        scroll_.apply(replay_inspection::ScrollKey::wheel_up, visible_lines_, lines.size());
        scroll_.apply(replay_inspection::ScrollKey::wheel_down, visible_lines_, lines.size());
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 16.0F, viewport->Pos.y + 16.0F),
                                ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.8F);
        ImGui::Begin("##debug", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
        const auto first = std::min(scroll_.first(), lines.size());
        const auto last = std::min(lines.size(), first + visible_lines_);
        for (auto index = first; index < last; ++index)
            ImGui::Text("%s  %s", lines[index].label.c_str(), lines[index].value.c_str());
        ImGui::End();
        ++debug_frames_;
    }
    ImGui::Render();
    const VkCommandBuffer command = frame.command_buffer();
    barrier_to_color(command, frame.image());
    VkClearValue clear{};
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = render_pass_;
    begin.framebuffer = *framebuffer;
    begin.renderArea = {{0, 0}, {width_, height_}};
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
    vkCmdEndRenderPass(command);
}

} // namespace ayther::runtime
