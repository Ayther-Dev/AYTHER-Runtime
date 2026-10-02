#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#if defined(AYTHER_SYNTHETIC_CORE_BUILD)
#define AYTHER_SYNTHETIC_API __declspec(dllexport)
#else
#define AYTHER_SYNTHETIC_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define AYTHER_SYNTHETIC_API __attribute__((visibility("default")))
#else
#define AYTHER_SYNTHETIC_API
#endif

struct retro_system_info {
    const char *library_name;
    const char *library_version;
    const char *valid_extensions;
    bool need_fullpath;
    bool block_extract;
};

struct retro_game_info {
    const char *path;
    const void *data;
    std::size_t size;
    const char *meta;
};

struct retro_game_geometry {
    unsigned base_width;
    unsigned base_height;
    unsigned max_width;
    unsigned max_height;
    float aspect_ratio;
};

struct retro_system_timing {
    double fps;
    double sample_rate;
};

struct retro_system_av_info {
    retro_game_geometry geometry;
    retro_system_timing timing;
};

using retro_environment_t = bool (*)(unsigned, void *);
using retro_video_refresh_t = void (*)(const void *, unsigned, unsigned, std::size_t);
using retro_audio_sample_t = void (*)(std::int16_t, std::int16_t);
using retro_audio_sample_batch_t = std::size_t (*)(const std::int16_t *, std::size_t);
using retro_input_poll_t = void (*)();
using retro_input_state_t = std::int16_t (*)(unsigned, unsigned, unsigned, unsigned);

inline constexpr std::uint32_t synthetic_state_magic = 0x41595451U;
inline constexpr std::uint32_t synthetic_state_version = 1;

struct SyntheticSerializedState {
    std::uint32_t magic{synthetic_state_magic};
    std::uint32_t version{synthetic_state_version};
    std::uint64_t frame{};
    std::uint64_t accumulator{};
    std::uint16_t last_input{};
    std::uint16_t reserved{};
};

struct SyntheticAudioWrite {
    std::uint32_t cycle;
    std::uint16_t address;
    std::uint8_t data;
    std::uint8_t chip;
};

inline constexpr unsigned synthetic_audio_writes_region = 0x109;
inline constexpr unsigned synthetic_audio_count_region = 0x10A;
inline constexpr unsigned synthetic_audio_mute_region = 0x10D;

extern "C" {
AYTHER_SYNTHETIC_API unsigned retro_api_version() noexcept;
AYTHER_SYNTHETIC_API void retro_get_system_info(retro_system_info *) noexcept;
AYTHER_SYNTHETIC_API void retro_get_system_av_info(retro_system_av_info *) noexcept;
AYTHER_SYNTHETIC_API void retro_set_environment(retro_environment_t) noexcept;
AYTHER_SYNTHETIC_API void retro_set_video_refresh(retro_video_refresh_t) noexcept;
AYTHER_SYNTHETIC_API void retro_set_audio_sample(retro_audio_sample_t) noexcept;
AYTHER_SYNTHETIC_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t) noexcept;
AYTHER_SYNTHETIC_API void retro_set_input_poll(retro_input_poll_t) noexcept;
AYTHER_SYNTHETIC_API void retro_set_input_state(retro_input_state_t) noexcept;
AYTHER_SYNTHETIC_API void retro_init() noexcept;
AYTHER_SYNTHETIC_API void retro_deinit() noexcept;
AYTHER_SYNTHETIC_API void retro_reset() noexcept;
AYTHER_SYNTHETIC_API bool retro_load_game(const retro_game_info *) noexcept;
AYTHER_SYNTHETIC_API void retro_unload_game() noexcept;
AYTHER_SYNTHETIC_API void retro_run() noexcept;
AYTHER_SYNTHETIC_API std::size_t retro_serialize_size() noexcept;
AYTHER_SYNTHETIC_API bool retro_serialize(void *, std::size_t) noexcept;
AYTHER_SYNTHETIC_API bool retro_unserialize(const void *, std::size_t) noexcept;
AYTHER_SYNTHETIC_API unsigned retro_get_region() noexcept;
AYTHER_SYNTHETIC_API void *retro_get_memory_data(unsigned) noexcept;
AYTHER_SYNTHETIC_API std::size_t retro_get_memory_size(unsigned) noexcept;
AYTHER_SYNTHETIC_API void retro_cheat_reset() noexcept;
AYTHER_SYNTHETIC_API void retro_cheat_set(unsigned, bool, const char *) noexcept;
}
