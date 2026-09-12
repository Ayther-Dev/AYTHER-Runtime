// Minimal libretro ABI fixture: no external ROM, audio device or GPU required.
#include <cstddef>
#include <cstdint>
#if defined(_WIN32)
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif
struct GameInfo { const char* path; const void* data; std::size_t size; const char* meta; };
struct SystemInfo { const char* name; const char* version; const char* extensions; bool fullpath; bool block; };
struct Geometry { unsigned width, height, max_width, max_height; float aspect; };
struct Timing { double fps, sample_rate; };
struct AvInfo { Geometry geometry; Timing timing; };
using Video = void (*)(const void*, unsigned, unsigned, std::size_t);
namespace { Video video{}; std::uint16_t pixels[64]{}; }
EXPORT unsigned retro_api_version() { return 1; }
EXPORT void retro_set_environment(bool (*)(unsigned, void*)) {}
EXPORT void retro_set_video_refresh(Video callback) { video = callback; }
EXPORT void retro_set_audio_sample(void (*)(std::int16_t, std::int16_t)) {}
EXPORT void retro_set_audio_sample_batch(std::size_t (*)(const std::int16_t*, std::size_t)) {}
EXPORT void retro_set_input_poll(void (*)()) {}
EXPORT void retro_set_input_state(std::int16_t (*)(unsigned, unsigned, unsigned, unsigned)) {}
EXPORT void retro_init() {}
EXPORT void retro_deinit() {}
EXPORT bool retro_load_game(const GameInfo*) { return true; }
EXPORT void retro_unload_game() {}
EXPORT void retro_run() { if (video) video(pixels, 8, 8, 16); }
EXPORT void* retro_get_memory_data(unsigned) { return nullptr; }
EXPORT std::size_t retro_get_memory_size(unsigned) { return 0; }
EXPORT void retro_cheat_set(unsigned, bool, const char*) {}
EXPORT void retro_cheat_reset() {}
EXPORT void retro_get_system_info(SystemInfo* info) {
    *info = {"Runtime Trust Fixture", "1", "rom", false, false};
}
EXPORT void retro_get_system_av_info(AvInfo* info) {
    *info = {{8, 8, 8, 8, 1.0F}, {60.0, 48000.0}};
}
