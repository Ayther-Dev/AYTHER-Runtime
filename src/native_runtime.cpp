#include "native_runtime.h"
#ifdef AYTHER_NATIVE_ALLOCATION_METRICS
#include "native_allocation_metrics.hpp"
#endif

#include <ayther/ayther_core_ffi.h>
#include <ayther/ayther_session.h>
#include <ayther/ayther_renderer.h>

#include "vulkan_backend/aspect_fit.h"
#include "vulkan_backend/vk_context.h"
#include "vulkan_backend/vk_present.h"
#include "vulkan_backend/vk_swapchain.h"

#include <SDL3/SDL.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace ayther::runtime {
namespace {

using Bytes = std::vector<std::uint8_t>;
namespace native = ayther::engine;

bool read_file(const std::string &name, Bytes &out) {
    std::ifstream file(name, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0 || file.tellg() > 64 * 1024 * 1024)
        return false;
    const auto size = static_cast<std::size_t>(file.tellg());
    out.resize(size);
    file.seekg(0);
    return static_cast<bool>(file.read(reinterpret_cast<char *>(out.data()),
                                       static_cast<std::streamsize>(size)));
}

void integer(Bytes &out, std::uint64_t value, unsigned count) {
    for (unsigned index = 0; index < count; ++index)
        out.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
}

void field(Bytes &out, std::uint32_t tag, std::uint32_t encoding,
           std::span<const std::uint8_t> data) {
    integer(out, tag, 4);
    integer(out, encoding, 4);
    integer(out, data.size(), 8);
    out.insert(out.end(), data.begin(), data.end());
}

void scalar(Bytes &out, std::uint32_t tag, std::uint32_t encoding,
            std::uint32_t value) {
    Bytes encoded;
    integer(encoded, value, 4);
    field(out, tag, encoding, encoded);
}

Bytes text(const std::string &value) { return {value.begin(), value.end()}; }

Bytes array(const std::vector<Bytes> &values) {
    Bytes out;
    integer(out, values.size(), 8);
    for (const auto &value : values) {
        integer(out, value.size(), 8);
        out.insert(out.end(), value.begin(), value.end());
    }
    return out;
}

int nibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool prepare(const Bytes &rom, native::NativeConfig &out, native::NativeResult &error) {
    native::NativeDetection detection;
    error = native::NativeEmulation::detect(rom, 0, detection);
    if (error.code != 0 || detection.system != 1)
        return false;
    std::vector<native::NativeBackendInfo> backends;
    error = native::NativeEmulation::backends(backends);
    if (error.code != 0)
        return false;
    const native::NativeBackendInfo *selected = nullptr;
    for (const auto &backend : backends)
        if (backend.id == "clownmdemu" && backend.system == detection.system)
            selected = &backend;
    if (!selected) {
        error.code = static_cast<std::int32_t>(native::NativeStatus::implementation_unavailable);
        return false;
    }

    out.backend_id = selected->id;
    out.system_override = detection.system;
    out.content = rom;
    out.initial_persistent_memory.resize(65536);
    scalar(out.config, 1, 1, detection.system);
    field(out.config, 2, 6, text(out.backend_id));
    field(out.config, 3, 5, selected->build_id);
    scalar(out.config, 4, 1, detection.system);
    scalar(out.config, 5, 1, 48000);
    std::vector<Bytes> inputs;
    for (std::uint32_t control = 0; control < 8; ++control) {
        Bytes item;
        scalar(item, 1, 1, 0);
        scalar(item, 2, 1, control);
        scalar(item, 3, 1, 1);
        scalar(item, 4, 2, 0);
        scalar(item, 5, 2, 1);
        inputs.push_back(std::move(item));
    }
    field(out.config, 6, 8, array(inputs));
    std::array<char, 65> digest{};
    if (!ayther_sha256_hex(out.initial_persistent_memory.data(),
                           out.initial_persistent_memory.size(), digest.data()))
        return false;
    Bytes hash(32);
    for (std::size_t index = 0; index < hash.size(); ++index) {
        const int hi = nibble(digest[index * 2]);
        const int lo = nibble(digest[index * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        hash[index] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    field(out.config, 7, 5, hash);
    scalar(out.config, 8, 1, 895780);
    scalar(out.config, 9, 1, 53693175);
    scalar(out.config, 10, 1, 1);
    scalar(out.config, 11, 2, 32768);
    scalar(out.config, 12, 2, 4096);
    field(out.config, 13, 6, text("integer-causal-ZOH-v1;CD-disabled-zero-required;zero-initialized-upstream-options"));
    return true;
}

std::uint64_t hash_bytes(std::span<const std::uint8_t> bytes) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const auto byte : bytes)
        value = (value ^ byte) * 1099511628211ULL;
    return value;
}

std::uint64_t read_u64(std::span<const std::uint8_t> bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 8) return 0;
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index)
        value |= std::uint64_t{bytes[at + index]} << (8 * index);
    return value;
}

double microseconds(std::chrono::steady_clock::time_point start,
                    std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::micro>(end - start).count();
}

void report_cost(const char *stage, std::vector<double> samples) {
    if (samples.empty()) return;
    const std::size_t warmup = samples.size() > 60 ? 30 : 0;
    samples.erase(samples.begin(), samples.begin() + warmup);
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&samples](std::size_t percent) {
        const auto rank = (samples.size() * percent + 99) / 100;
        return samples[rank - 1];
    };
    std::fprintf(stdout,
        "AYTHER_NATIVE_COST stage=%s unit=us warmup=%zu count=%zu p50=%.3f p95=%.3f p99=%.3f max=%.3f\n",
        stage, warmup, samples.size(), percentile(50), percentile(95),
        percentile(99), samples.back());
}

class NativeDevices final {
  public:
    NativeDevices() = default;
    NativeDevices(const NativeDevices &) = delete;
    NativeDevices &operator=(const NativeDevices &) = delete;
    ~NativeDevices() {
        armed_.store(false);
        if (audio_) SDL_DestroyAudioStream(audio_);
        if (gpu_query_pool_ != VK_NULL_HANDLE && context_.device() != VK_NULL_HANDLE) {
            (void)context_.wait_idle("vkDeviceWaitIdle [NativeDevices timestamps]");
            vkDestroyQueryPool(context_.device(), gpu_query_pool_, nullptr);
        }
        if (renderer_.is_ready()) renderer_.shutdown(context_.engine_view());
        swapchain_.shutdown();
        context_.shutdown();
        if (window_) SDL_DestroyWindow(window_);
        if (sdl_ready_) SDL_Quit();
    }

    bool initialize(bool visible, bool audio_device, bool gpu_probe) {
        if (gpu_probe && !visible) return false;
        if (!visible && !audio_device) return true;
        const auto flags = (visible ? SDL_INIT_VIDEO : 0U) |
                           (audio_device ? SDL_INIT_AUDIO : 0U);
        if (!SDL_Init(flags)) return false;
        sdl_ready_ = true;
        if (visible) {
            window_ = SDL_CreateWindow("AYTHER Tessera", 960, 720,
                                       SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
            if (!window_ || !context_.init(window_)) return false;
            int width = 0, height = 0;
            SDL_GetWindowSizeInPixels(window_, &width, &height);
            if (width <= 0 || height <= 0 ||
                !swapchain_.init(context_, static_cast<std::uint32_t>(width),
                                 static_cast<std::uint32_t>(height))) return false;
            const auto canvas = aspect_fit(4, 3, swapchain_.extent().width,
                                           swapchain_.extent().height);
            const char *base = SDL_GetBasePath();
            const std::filesystem::path shaders =
                (base ? std::filesystem::path(base) : std::filesystem::current_path()) /
                "shaders";
            const auto shader_directory = shaders.generic_string() + "/";
            if (!renderer_.init(context_.engine_view(),
                                static_cast<std::uint32_t>(canvas.w),
                                static_cast<std::uint32_t>(canvas.h),
                                shader_directory.c_str())) return false;
            if (gpu_probe && !renderer_.readback_init(context_.engine_view())) return false;
            std::uint32_t family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(context_.physical_device(),
                                                     &family_count, nullptr);
            std::vector<VkQueueFamilyProperties> families(family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(context_.physical_device(),
                                                     &family_count, families.data());
            if (context_.graphics_family() < family_count) {
                timestamp_valid_bits_ = families[context_.graphics_family()].timestampValidBits;
                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(context_.physical_device(), &properties);
                timestamp_period_ns_ = properties.limits.timestampPeriod;
                if (timestamp_valid_bits_ != 0 && timestamp_period_ns_ > 0.0f) {
                    VkQueryPoolCreateInfo query_info{};
                    query_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
                    query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
                    query_info.queryCount = 3 * VkSwapchain::kMaxFrames;
                    if (vkCreateQueryPool(context_.device(), &query_info, nullptr,
                                          &gpu_query_pool_) != VK_SUCCESS) return false;
                }
            }
        }
        if (audio_device) {
            const SDL_AudioSpec input{SDL_AUDIO_S16, 2, 48000};
            audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                               &input, audio_callback, this);
            if (!audio_) return false;
        }
        return true;
    }

    bool present(const ayther::FrameView &frame) {
        if (!window_) return true;
        SDL_Event event{};
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                return false;
        const auto acquire_start = std::chrono::steady_clock::now();
        auto acquired = swapchain_.begin_frame(context_);
        if (!acquired) return false;
        const auto slot = static_cast<std::uint32_t>(presented_ % VkSwapchain::kMaxFrames);
        if (gpu_query_pool_ != VK_NULL_HANDLE) {
            if (!collect_gpu_query(slot)) return false;
            vkCmdResetQueryPool(acquired->command_buffer(), gpu_query_pool_,
                                slot * 3, 3);
            vkCmdWriteTimestamp(acquired->command_buffer(),
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                gpu_query_pool_, slot * 3);
            // The submit waits for image_ready at COLOR_ATTACHMENT_OUTPUT.
            // This timestamp enters that stage after the semaphore gate.
            vkCmdWriteTimestamp(acquired->command_buffer(),
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                gpu_query_pool_, slot * 3 + 1);
        }
        const auto record_start = std::chrono::steady_clock::now();
        renderer_.render(context_.engine_view(), acquired->command_buffer(),
                         frame, {}, false);
        VkPresent::blit_to_swapchain(context_, *acquired, renderer_.render_image());
        VkPresent::finalize(context_, *acquired);
        if (gpu_query_pool_ != VK_NULL_HANDLE)
            vkCmdWriteTimestamp(acquired->command_buffer(),
                                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                gpu_query_pool_, slot * 3 + 2);
        const auto submit_start = std::chrono::steady_clock::now();
        if (!swapchain_.end_frame(context_, *acquired)) return false;
        if (gpu_query_pool_ != VK_NULL_HANDLE) gpu_query_pending_[slot] = true;
        const auto now = std::chrono::steady_clock::now();
        gpu_acquire_cpu_.push_back(microseconds(acquire_start, record_start));
        gpu_record_cpu_.push_back(microseconds(record_start, submit_start));
        gpu_submit_cpu_.push_back(microseconds(submit_start, now));
        gpu_staging_bytes_ += static_cast<std::uint64_t>(frame.fb_width) *
                              frame.fb_height * 4;
        if (last_present_ != std::chrono::steady_clock::time_point{})
            intervals_.push_back(std::chrono::duration<double, std::milli>(
                now - last_present_).count());
        last_present_ = now;
        ++presented_;
        return true;
    }

    bool probe(const ayther::FrameView &frame, std::uint64_t number) {
        if (!window_) return false;
        const auto *pixels = renderer_.export_frame(context_.engine_view(), frame, {}, false);
        if (!pixels) return false;
        const auto extent = renderer_.framebuffer_extent();
        const auto count = static_cast<std::size_t>(extent.width) * extent.height;
        std::size_t magenta = 0, cyan = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const auto *pixel = pixels + index * 4;
            if (pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 255) ++magenta;
            if (pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 0) ++cyan;
        }
        std::fprintf(stdout,
            "AYTHER_NATIVE_GPU_FRAME frame=%llu hash=%016llx magenta_pixels=%zu cyan_pixels=%zu\n",
            static_cast<unsigned long long>(number),
            static_cast<unsigned long long>(hash_bytes({pixels, count * 4})),
            magenta, cyan);
        return true;
    }

    bool queue_audio(const native::NativeFrame &frame) {
        if (!audio_) return true;
        if (frame.sample_rate != 48000 || frame.channels != 2 ||
            frame.pcm.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return false;
        if (!frame.pcm.empty() &&
            !SDL_PutAudioStreamData(audio_, frame.pcm.data(),
                                    static_cast<int>(frame.pcm.size()))) return false;
        audio_stream_bytes_ += frame.pcm.size();
        for (std::size_t index = 0; index + 1 < frame.pcm.size(); index += 2) {
            const auto value = static_cast<std::int16_t>(
                static_cast<std::uint16_t>(frame.pcm[index]) |
                (static_cast<std::uint16_t>(frame.pcm[index + 1]) << 8));
            const auto magnitude = value < 0 ? -static_cast<int>(value) :
                                                static_cast<int>(value);
            source_peak_ = std::max(source_peak_, magnitude);
            if (magnitude > 16) ++active_samples_;
        }
        audio_frames_ += frame.pcm_frames;
        if (!audio_started_ && audio_frames_ >= 4 * 800) {
            armed_.store(true);
            if (!SDL_ResumeAudioStreamDevice(audio_)) return false;
            audio_started_ = true;
        }
        return true;
    }

    void report() {
        if (window_) {
            if (gpu_query_pool_ != VK_NULL_HANDLE) {
                if (const auto failure = context_.wait_idle(
                        "vkDeviceWaitIdle [NativeDevices timestamp report]")) {
                    ayther::runtime::vulkan::log_vk_failure(*failure);
                } else {
                    for (std::uint32_t slot = 0; slot < VkSwapchain::kMaxFrames; ++slot)
                        if (!collect_gpu_query(slot))
                            std::fprintf(stderr, "[native] GPU timestamp read failed.\n");
                }
            }
            std::fprintf(stdout,
                "AYTHER_NATIVE_GPU_TIMESTAMP supported=%u valid_bits=%u period_ns=%.6f samples=%zu scope=top_image_ready_bottom\n",
                gpu_query_pool_ != VK_NULL_HANDLE ? 1U : 0U,
                timestamp_valid_bits_, static_cast<double>(timestamp_period_ns_),
                gpu_execution_costs_.size());
            report_cost("gpu_execute", gpu_execution_costs_);
            report_cost("gpu_image_wait", gpu_image_wait_costs_);
            report_cost("gpu_command_span", gpu_command_span_costs_);
            auto ordered = intervals_;
            std::sort(ordered.begin(), ordered.end());
            const auto percentile = [&ordered](double fraction) {
                if (ordered.empty()) return 0.0;
                const auto index = static_cast<std::size_t>(fraction *
                    static_cast<double>(ordered.size() - 1));
                return ordered[index];
            };
            std::fprintf(stdout,
                "AYTHER_NATIVE_PRESENT gpu=\"%s\" frames=%llu intervals=%zu p50_ms=%.3f p95_ms=%.3f p99_ms=%.3f max_ms=%.3f validation_errors=%u effective_timestamps=unavailable\n",
                context_.gpu_name().c_str(),
                static_cast<unsigned long long>(presented_), intervals_.size(),
                percentile(0.50), percentile(0.95), percentile(0.99),
                ordered.empty() ? 0.0 : ordered.back(), context_.validation_error_count());
            report_cost("gpu_acquire_cpu", gpu_acquire_cpu_);
            report_cost("gpu_record_cpu", gpu_record_cpu_);
            report_cost("gpu_submit_present_cpu", gpu_submit_cpu_);
        }
        std::fprintf(stdout,
            "AYTHER_NATIVE_DEVICE_BYTES gpu_staging=%llu audio_stream_queue=%llu scope=runtime_explicit_boundaries\n",
            static_cast<unsigned long long>(gpu_staging_bytes_),
            static_cast<unsigned long long>(audio_stream_bytes_));
        if (audio_) {
            const auto missing = missing_frames_.load();
            const auto max_episode = max_episode_frames_.load();
            std::fprintf(stdout,
                "AYTHER_NATIVE_AUDIO device=%u source_frames=%llu source_peak=%d active_samples=%llu callback_calls=%llu episodes=%llu missing_samples=%llu total_ms=%.3f max_ms=%.3f\n",
                SDL_GetAudioStreamDevice(audio_),
                static_cast<unsigned long long>(audio_frames_),
                source_peak_, static_cast<unsigned long long>(active_samples_),
                static_cast<unsigned long long>(callback_calls_.load()),
                static_cast<unsigned long long>(episodes_.load()),
                static_cast<unsigned long long>(missing),
                static_cast<double>(missing) * 1000.0 / 48000.0,
                static_cast<double>(max_episode) * 1000.0 / 48000.0);
        }
    }

  private:
    bool collect_gpu_query(std::uint32_t slot) {
        if (!gpu_query_pending_[slot]) return true;
        std::array<std::uint64_t, 3> ticks{};
        if (vkGetQueryPoolResults(context_.device(), gpu_query_pool_, slot * 3, 3,
                                  sizeof(ticks), ticks.data(), sizeof(ticks[0]),
                                  VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return false;
        const std::uint64_t mask = timestamp_valid_bits_ == 64
            ? std::numeric_limits<std::uint64_t>::max()
            : (std::uint64_t{1} << timestamp_valid_bits_) - 1;
        const auto wait_ticks = (ticks[1] - ticks[0]) & mask;
        const auto execution_ticks = (ticks[2] - ticks[1]) & mask;
        const auto span_ticks = (ticks[2] - ticks[0]) & mask;
        gpu_execution_costs_.push_back(
            static_cast<double>(execution_ticks) * timestamp_period_ns_ / 1000.0);
        gpu_image_wait_costs_.push_back(
            static_cast<double>(wait_ticks) * timestamp_period_ns_ / 1000.0);
        gpu_command_span_costs_.push_back(
            static_cast<double>(span_ticks) * timestamp_period_ns_ / 1000.0);
        gpu_query_pending_[slot] = false;
        return true;
    }

    static void SDLCALL audio_callback(void *user, SDL_AudioStream *, int additional,
                                       int) {
        auto &self = *static_cast<NativeDevices *>(user);
        if (!self.armed_.load()) return;
        self.callback_calls_.fetch_add(1, std::memory_order_relaxed);
        if (additional <= 0) {
            self.episode_active_.store(false);
            self.current_episode_frames_.store(0);
            return;
        }
        const auto missing = static_cast<std::uint64_t>(additional + 3) / 4;
        self.missing_frames_.fetch_add(missing);
        if (!self.episode_active_.exchange(true)) self.episodes_.fetch_add(1);
        const auto accumulated = self.current_episode_frames_.fetch_add(missing) + missing;
        auto previous = self.max_episode_frames_.load();
        while (accumulated > previous &&
               !self.max_episode_frames_.compare_exchange_weak(previous, accumulated)) {}
    }

    bool sdl_ready_{};
    SDL_Window *window_{};
    VkContext context_;
    VkSwapchain swapchain_;
    ayther::AytherRenderer renderer_;
    SDL_AudioStream *audio_{};
    bool audio_started_{};
    std::uint64_t audio_frames_{}, active_samples_{}, presented_{};
    int source_peak_{};
    std::chrono::steady_clock::time_point last_present_{};
    std::vector<double> intervals_;
    std::vector<double> gpu_acquire_cpu_, gpu_record_cpu_, gpu_submit_cpu_;
    std::vector<double> gpu_execution_costs_;
    std::vector<double> gpu_image_wait_costs_, gpu_command_span_costs_;
    VkQueryPool gpu_query_pool_{VK_NULL_HANDLE};
    std::array<bool, VkSwapchain::kMaxFrames> gpu_query_pending_{};
    std::uint32_t timestamp_valid_bits_{};
    float timestamp_period_ns_{};
    std::uint64_t gpu_staging_bytes_{}, audio_stream_bytes_{};
    std::atomic<bool> armed_{false}, episode_active_{false};
    std::atomic<std::uint64_t> callback_calls_{0}, episodes_{0}, missing_frames_{0},
        current_episode_frames_{0}, max_episode_frames_{0};
};

} // namespace

int run_native_runtime(const RuntimeOptions &options) {
    if (options.rom_path.empty() || (options.frames_limit == 0 && options.native_trace_path.empty()) ||
        !options.core_path.empty() || !options.patch_path.empty() || !options.core_options.empty() ||
        !options.input_map_path.empty() || !options.load_state.empty() ||
        !options.profile.empty() || options.subsystems.has_value() ||
        options.mute_buses.has_value() || options.shaders.has_value() ||
        !options.output.empty() || !options.capture_at.empty() ||
        !options.manifest_path.empty() || options.hd_compose ||
        (options.native_reactivate_at != 0 &&
         options.native_reactivate_at <= options.native_deactivate_at)) {
        std::fprintf(stderr, "[native] ROM y --frames o --native-trace son obligatorios; opciones libretro incompatibles.\n");
        return runtime_cli_error_exit_code;
    }
    Bytes rom, trace;
    if (!read_file(options.rom_path, rom) ||
        (!options.native_trace_path.empty() && !read_file(options.native_trace_path, trace))) {
        std::fprintf(stderr, "[native] No se pudo leer ROM o traza.\n");
        return 1;
    }
    native::NativeConfig configuration;
    native::NativeResult status;
    if (!prepare(rom, configuration, status)) {
        std::fprintf(stderr, "[native] Detección o preparación rechazada: %d\n", status.code);
        return 1;
    }
    std::array<char, 65> config_digest{};
    if (!ayther_sha256_hex(configuration.config.data(),
                           configuration.config.size(), config_digest.data())) return 1;
    std::fprintf(stdout, "AYTHER_NATIVE_CONFIG sha256=%s\n", config_digest.data());
    ayther::AytherSession::Config request;
    request.provider = ayther::EmulationProvider::tessera_native;
    request.native_config = std::move(configuration);
    request.pack_path = options.pack_path;
    request.trust_registry = options.trust_registry_path;
    request.enable_audio = false;
    request.derive_core_pack = false;
    auto created = ayther::AytherSession::create(request, status);
    if (!created || status.code != 0) {
        std::fprintf(stderr, "[native] Engine rechazó la sesión: %d\n", status.code);
        return 1;
    }
    auto session = std::move(*created);
    NativeDevices devices;
    if (!devices.initialize(options.native_visible, options.native_audio_device,
                            options.native_gpu_probe)) {
        std::fprintf(stderr, "[native] No se pudo abrir Vulkan o el dispositivo de audio: %s\n",
                     SDL_GetError());
        return 1;
    }
    auto *emulation = session->native_emulation();
    if (!emulation || (!options.pack_path.empty() && !session->has_pack())) {
        std::fprintf(stderr, "[native] El proveedor o pack solicitado no está activo.\n");
        return 1;
    }
    if (!trace.empty() && (status = emulation->bind_trace(trace)).code != 0) {
        std::fprintf(stderr, "[native] Traza rechazada: %d\n", status.code);
        return 1;
    }
    status = session->native_submit_presentation_policy(31, 31);
    if (status.code != 0) {
        std::fprintf(stderr, "[native] Política inicial rechazada: %d\n", status.code);
        return 1;
    }
    std::uint64_t completed = 0;
    std::uint64_t borrowed_video_bytes = 0, composed_copy_bytes = 0;
    std::uint64_t composed_frames = 0, fallback_frames = 0;
    std::array<std::uint64_t, 11> fallback_reasons{};
    std::vector<double> step_costs, frame_costs, hash_costs, device_costs;
    if (options.frames_limit > 0 && options.frames_limit <= 65536) {
        const auto reserve = static_cast<std::size_t>(options.frames_limit);
        step_costs.reserve(reserve);
        frame_costs.reserve(reserve);
        hash_costs.reserve(reserve);
        device_costs.reserve(reserve);
    }
    const auto pace_start = std::chrono::steady_clock::now();
    for (;;) {
        if (options.frames_limit != 0 && completed >= options.frames_limit)
            break;
#ifdef AYTHER_NATIVE_ALLOCATION_METRICS
        const auto allocation_before = allocation_metrics::snapshot();
#endif
        const auto next = completed + 1;
        if (next == options.native_deactivate_at || next == options.native_reactivate_at) {
            status = session->native_submit_presentation_policy(
                next == options.native_deactivate_at ? 0 : 31,
                next == options.native_deactivate_at ? 0 : 31);
            if (status.code != 0) {
                std::fprintf(stderr, "[native] Control rechazado: %d\n", status.code);
                return 1;
            }
        }
        const auto step_start = std::chrono::steady_clock::now();
        if (!trace.empty()) {
            status = session->native_replay_next();
        } else {
            native::NativeSessionInfo current;
            status = emulation->info(current);
            if (status.code != 0 || current.frame_id.frame == std::numeric_limits<std::uint64_t>::max())
                return 1;
            std::array<native::NativeInputAssignment, 8> inputs{};
            for (std::uint16_t control = 0; control < inputs.size(); ++control)
                inputs[control] = {0, control,
                    static_cast<std::int32_t>((options.native_input_mask >> control) & 1U)};
            status = session->native_step_frame({current.frame_id.epoch,
                                                 current.frame_id.frame + 1, inputs});
        }
        if (status.code == static_cast<std::int32_t>(native::NativeStatus::end_of_trace)) {
            std::fprintf(stdout, "AYTHER_NATIVE_EOF frame=%llu\n",
                         static_cast<unsigned long long>(completed));
            break;
        }
        if (status.code != 0) {
            std::fprintf(stderr, "[native] Frame rechazado: code=%d frame=%llu transaction=%llu\n",
                         status.code, static_cast<unsigned long long>(status.frame_id.frame),
                         static_cast<unsigned long long>(status.transaction));
            return 1;
        }
        const auto step_end = std::chrono::steady_clock::now();
        const auto &frame = session->native_frame();
        if (frame.native_result.code != 0 || frame.fb_pixels == nullptr) {
            std::fprintf(stderr, "[native] Frame original ausente.\n");
            return 1;
        }
        const auto frame_end = std::chrono::steady_clock::now();
        const auto presented = std::span<const std::uint8_t>{
            static_cast<const std::uint8_t *>(frame.fb_pixels),
            static_cast<std::size_t>(frame.fb_pitch) * frame.fb_height};
        borrowed_video_bytes += frame.native_original.pixels.size();
        if (frame.native_original_reason == ayther::NativeOriginalReason::none) {
            ++composed_frames;
            composed_copy_bytes += presented.size();
        } else {
            ++fallback_frames;
            const auto reason = static_cast<std::size_t>(frame.native_original_reason);
            if (reason < fallback_reasons.size()) ++fallback_reasons[reason];
        }
        const auto hash_start = std::chrono::steady_clock::now();
        const auto original_hash = hash_bytes(frame.native_original.pixels);
        const auto presented_hash = hash_bytes(presented);
        const auto pcm_hash = hash_bytes(frame.native_original.pcm);
        const auto hash_end = std::chrono::steady_clock::now();
        std::fprintf(stdout,
                     "AYTHER_NATIVE_FRAME frame=%llu original=%016llx presented=%016llx pcm=%016llx reason=%u transaction=%llu flags=%u\n",
                     static_cast<unsigned long long>(status.frame_id.frame),
                     static_cast<unsigned long long>(original_hash),
                     static_cast<unsigned long long>(presented_hash),
                     static_cast<unsigned long long>(pcm_hash),
                     static_cast<unsigned>(frame.native_original_reason),
                     static_cast<unsigned long long>(status.transaction), status.flags);
        const auto device_start = std::chrono::steady_clock::now();
        if (!devices.queue_audio(frame.native_original) || !devices.present(frame) ||
            (options.native_gpu_probe && !devices.probe(frame, completed + 1))) {
            std::fprintf(stderr, "[native] Falló la entrega de audio o presentación Vulkan.\n");
            return 1;
        }
        const auto device_end = std::chrono::steady_clock::now();
#ifdef AYTHER_NATIVE_ALLOCATION_METRICS
        const auto allocation_after = allocation_metrics::snapshot();
        std::fprintf(stdout,
            "AYTHER_NATIVE_ALLOC frame=%llu calls=%llu bytes=%llu scope=executable_cpp_new\n",
            static_cast<unsigned long long>(completed + 1),
            static_cast<unsigned long long>(allocation_after.calls - allocation_before.calls),
            static_cast<unsigned long long>(allocation_after.bytes - allocation_before.bytes));
#endif
        step_costs.push_back(microseconds(step_start, step_end));
        frame_costs.push_back(microseconds(step_end, frame_end));
        hash_costs.push_back(microseconds(hash_start, hash_end));
        device_costs.push_back(microseconds(device_start, device_end));
        ++completed;
        if (options.native_visible || options.native_audio_device)
            std::this_thread::sleep_until(pace_start +
                std::chrono::nanoseconds(16'688'754LL * static_cast<std::int64_t>(completed)));
    }
    devices.report();
    std::fprintf(stdout,
        "AYTHER_NATIVE_FLOW frames=%llu borrowed_video=%llu composed_frames=%llu composed_copy=%llu fallback_frames=%llu fallback_no_pack=%llu fallback_policy=%llu fallback_no_rule=%llu scope=engine_runtime_explicit_boundaries\n",
        static_cast<unsigned long long>(completed),
        static_cast<unsigned long long>(borrowed_video_bytes),
        static_cast<unsigned long long>(composed_frames),
        static_cast<unsigned long long>(composed_copy_bytes),
        static_cast<unsigned long long>(fallback_frames),
        static_cast<unsigned long long>(fallback_reasons[static_cast<std::size_t>(ayther::NativeOriginalReason::no_pack)]),
        static_cast<unsigned long long>(fallback_reasons[static_cast<std::size_t>(ayther::NativeOriginalReason::policy_unconfirmed)]),
        static_cast<unsigned long long>(fallback_reasons[static_cast<std::size_t>(ayther::NativeOriginalReason::no_matching_rule)]));
    std::uint64_t event_required = 0, event_next = 0, loss_required = 0;
    const auto event_probe = emulation->events(0, {}, event_required, event_next);
    if (event_probe.code != static_cast<std::int32_t>(native::NativeStatus::buffer_too_small) ||
        event_required < 24 || event_required > 16 * 1024 * 1024)
        return 1;
    Bytes event_batch(static_cast<std::size_t>(event_required));
    if (emulation->events(0, event_batch, event_required, event_next).code != 0 ||
        event_required != event_batch.size()) return 1;
    const auto loss_probe = emulation->losses({}, loss_required);
    if (loss_probe.code != static_cast<std::int32_t>(native::NativeStatus::buffer_too_small) ||
        loss_required < 24 || loss_required > 4096) return 1;
    Bytes loss_data(static_cast<std::size_t>(loss_required));
    if (emulation->losses(loss_data, loss_required).code != 0 ||
        loss_required != loss_data.size()) return 1;
    std::fprintf(stdout,
        "AYTHER_NATIVE_EVENTS emitted=%llu retained=%llu batch_bytes=%llu losses=%llu scope=post_run_engine_tessera_cursor\n",
        static_cast<unsigned long long>(event_next),
        static_cast<unsigned long long>(read_u64(event_batch, 16)),
        static_cast<unsigned long long>(event_batch.size()),
        static_cast<unsigned long long>(read_u64(loss_data, 16)));
    report_cost("engine_step", std::move(step_costs));
    report_cost("engine_frame", std::move(frame_costs));
    report_cost("runtime_hash", std::move(hash_costs));
    report_cost("runtime_device", std::move(device_costs));
    std::fprintf(stdout, "AYTHER_NATIVE_DONE frames=%llu\n",
                 static_cast<unsigned long long>(completed));
    return 0;
}

} // namespace ayther::runtime
