#include "audio_qa_presentation.h"

#include "pack_layers.h"
#include "vulkan_backend/aspect_fit.h"
#include "vulkan_backend/vk_present.h"

#include <filesystem>

namespace ayther::runtime {

AudioQaPresentation::~AudioQaPresentation() {
    auto &context = presentation_.context();
    if (const auto failure = context.wait_idle("AudioQaPresentation teardown"))
        vulkan::log_vk_failure(*failure);
    if (renderer_ready_)
        renderer_.shutdown(context.engine_view());
    presentation_.shutdown();
    if (window_)
        SDL_DestroyWindow(window_);
    if (video_initialized_)
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void AudioQaPresentation::degrade(const std::string_view code) {
    if (report_.code == "presented")
        report_.code = code;
}

void AudioQaPresentation::initialize(AytherSession &session, const RuntimeOptions &options,
                                     const PlayerConfig &config) {
    report_.mode = "visible";
    report_.code = "presented";
    if (const auto *backend = SDL_GetCurrentAudioDriver())
        report_.audio_backend = backend;
    if (report_.audio_backend.empty() || report_.audio_backend == "dummy" ||
        report_.audio_backend == "disk")
        degrade("audible_output_unavailable");
    shaders_ = options.shaders.value_or(config.shaders_on);
    hd_ = config.hd_on;
    report_.hd_enabled = hd_;
    report_.shaders_enabled = shaders_;
    const auto pack = session.pack();
    profile_ = &output_profile_resolve(options.output.empty() ? config.output : options.output,
                                       pack ? pack.info().recommended_output_profile : "");
    report_.output_profile = profile_->id;
    video_initialized_ = SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (!video_initialized_) {
        degrade("video_initialization_failed");
        return;
    }
    // A normal Runtime Vulkan surface, with window controls available during QA.
    window_ = SDL_CreateWindow("AYTHER Runtime - QA", 1280, 720,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN);
    if (!window_) {
        degrade("window_creation_failed");
        return;
    }
    auto &context = presentation_.context();
    auto &swapchain = presentation_.swapchain();
    int width{}, height{};
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    report_.initial_width = width > 0 ? static_cast<std::uint32_t>(width) : 0U;
    report_.initial_height = height > 0 ? static_cast<std::uint32_t>(height) : 0U;
    if (!context.init(window_) || width <= 0 || height <= 0 ||
        !swapchain.init(context, static_cast<std::uint32_t>(width),
                        static_cast<std::uint32_t>(height))) {
        degrade("swapchain_initialization_failed");
        return;
    }
    const char *base = SDL_GetBasePath();
    const auto shaders = std::filesystem::path{base ? base : "."} / "shaders";
    const auto canvas = aspect_fit(4, 3, swapchain.extent().width, swapchain.extent().height);
    renderer_ready_ = renderer_.init(context.engine_view(), static_cast<std::uint32_t>(canvas.w),
                                     static_cast<std::uint32_t>(canvas.h),
                                     (shaders.generic_string() + "/").c_str());
    if (!renderer_ready_) {
        degrade("renderer_initialization_failed");
        return;
    }
    if (pack)
        pack.select_render_tier_for_height(static_cast<std::uint32_t>(canvas.h));
    if (ayther_runtime::build_pack_overlay_stack(session.pack_overlays(), layers_) != 0U)
        active_layers_ = &layers_;
    auto &postprocess = presentation_.postprocess();
    postprocess_ready_ =
        postprocess.init(context, swapchain, (shaders / "postprocess.vert.spv").string().c_str(),
                         (shaders / "postprocess.frag.spv").string().c_str());
    if (postprocess_ready_)
        postprocess.set_source(context, renderer_.render_image());
    else
        degrade("postprocess_unavailable");
    ready_ = true;
}

bool AudioQaPresentation::poll(const bool allow_cancel) {
    if (!video_initialized_)
        return true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED && !event.adevice.recording)
            degrade("audio_device_removed");
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && window_ &&
             event.window.windowID == SDL_GetWindowID(window_))) {
            if (allow_cancel)
                report_.cancelled = true;
            return false;
        }
        // All keyboard, gamepad and gameplay shortcuts are intentionally ignored.
    }
    return true;
}

bool AudioQaPresentation::rebuild(const int width, const int height) {
    auto &context = presentation_.context();
    auto &swapchain = presentation_.swapchain();
    if (width <= 0 || height <= 0 ||
        !swapchain.rebuild(context, static_cast<std::uint32_t>(width),
                           static_cast<std::uint32_t>(height)))
        return false;
    const auto canvas = aspect_fit(4, 3, swapchain.extent().width, swapchain.extent().height);
    if (!renderer_.resize(context.engine_view(), static_cast<std::uint32_t>(canvas.w),
                          static_cast<std::uint32_t>(canvas.h)))
        return false;
    if (postprocess_ready_) {
        postprocess_ready_ = presentation_.postprocess().rebuild(context, swapchain);
        if (postprocess_ready_)
            presentation_.postprocess().set_source(context, renderer_.render_image());
        else
            degrade("postprocess_unavailable");
    }
    return true;
}

void AudioQaPresentation::present(AytherSession &session, const FrameView &view,
                                  const std::uint32_t recording_frame) {
    if (!ready_ || !view.fb_pixels || (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED)) {
        report_.affect(recording_frame);
        degrade("video_frame_unavailable");
        return;
    }
    auto &context = presentation_.context();
    auto &swapchain = presentation_.swapchain();
    int width{}, height{};
    SDL_GetWindowSizeInPixels(window_, &width, &height);
    if (width <= 0 || height <= 0 ||
        ((static_cast<std::uint32_t>(width) != swapchain.extent().width ||
          static_cast<std::uint32_t>(height) != swapchain.extent().height) &&
         !rebuild(width, height))) {
        report_.affect(recording_frame);
        degrade("video_resize_failed");
        return;
    }
    auto acquired = swapchain.begin_frame(context);
    if (!acquired) {
        report_.affect(recording_frame);
        degrade("video_acquire_failed");
        (void)rebuild(width, height);
        return;
    }
    auto &frame = *acquired;
    renderer_.render(context.engine_view(), frame.command_buffer(), view, session.pack(), hd_,
                     active_layers_);
    if (postprocess_ready_) {
        const auto &sp = view.shader_params;
        const auto fx =
            shaders_ ? output_shader(*profile_, sp.crt_strength, sp.scan_strength, sp.vignette)
                     : OutputShader{};
        const auto emu_height = view.fb_height ? view.fb_height : 240U;
        const bool integer = profile_->scaling == OutputScaling::Integer;
        const auto rect = output_rect(*profile_, integer ? (emu_height * 4U + 1U) / 3U : 4U,
                                      integer ? emu_height : 3U, swapchain.extent().width,
                                      swapchain.extent().height);
        presentation_.postprocess().apply(
            context, frame, static_cast<float>(width), static_cast<float>(height),
            static_cast<float>(emu_height), static_cast<float>(SDL_GetTicks()) * 0.001f,
            OutDestRect{rect.x, rect.y, rect.w, rect.h}, profile_->smoothing, fx.crt, fx.scan,
            fx.vignette, fx.ntsc);
    } else {
        VkPresent::blit_to_swapchain(context, frame, renderer_.render_image(),
                                     profile_->scaling == OutputScaling::Integer,
                                     profile_->smoothing);
    }
    VkPresent::finalize(context, frame);
    if (swapchain.end_frame(context, frame)) {
        ++report_.presented_frames;
    } else {
        report_.affect(recording_frame);
        degrade("video_present_failed");
        (void)rebuild(width, height);
    }
}

} // namespace ayther::runtime
