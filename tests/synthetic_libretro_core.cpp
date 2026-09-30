#include "synthetic_libretro_core_api.h"

#include <array>
#include <cstring>

namespace {

retro_environment_t environment_callback{};
retro_video_refresh_t video_callback{};
retro_audio_sample_t audio_callback{};
retro_audio_sample_batch_t audio_batch_callback{};
retro_input_poll_t input_poll_callback{};
retro_input_state_t input_state_callback{};
SyntheticSerializedState state;
std::uint64_t rom_identity{};
std::array<std::uint32_t, 4> pixels{};
std::array<std::int16_t, 4> audio{};
std::array<SyntheticAudioWrite, 3> audio_writes{};
std::uint32_t audio_write_count{};
std::uint32_t audio_mute{};

void reset_state() noexcept {
    state = {};
    state.accumulator = 0x9e3779b97f4a7c15ULL ^ rom_identity;
    audio_write_count = 0;
    audio_mute = 0;
}

} // namespace

extern "C" AYTHER_SYNTHETIC_API unsigned retro_api_version() noexcept { return 1U; }

extern "C" AYTHER_SYNTHETIC_API void retro_get_system_info(retro_system_info *const info) noexcept {
    if (info != nullptr)
        *info = {"AYTHER Synthetic Core", "1.0-test", "aytest|rom", false, false};
}

extern "C" AYTHER_SYNTHETIC_API void
retro_get_system_av_info(retro_system_av_info *const info) noexcept {
    if (info != nullptr)
        *info = {{2, 2, 2, 2, 1.0F}, {60.0, 48000.0}};
}

extern "C" AYTHER_SYNTHETIC_API void
retro_set_environment(const retro_environment_t callback) noexcept {
    environment_callback = callback;
}

extern "C" AYTHER_SYNTHETIC_API void
retro_set_video_refresh(const retro_video_refresh_t callback) noexcept {
    video_callback = callback;
}

extern "C" AYTHER_SYNTHETIC_API void
retro_set_audio_sample(const retro_audio_sample_t callback) noexcept {
    audio_callback = callback;
}

extern "C" AYTHER_SYNTHETIC_API void
retro_set_audio_sample_batch(const retro_audio_sample_batch_t callback) noexcept {
    audio_batch_callback = callback;
}

extern "C" AYTHER_SYNTHETIC_API void
retro_set_input_poll(const retro_input_poll_t callback) noexcept {
    input_poll_callback = callback;
}

extern "C" AYTHER_SYNTHETIC_API void
retro_set_input_state(const retro_input_state_t callback) noexcept {
    input_state_callback = callback;
}

extern "C" AYTHER_SYNTHETIC_API void retro_init() noexcept { reset_state(); }
extern "C" AYTHER_SYNTHETIC_API void retro_deinit() noexcept {}
extern "C" AYTHER_SYNTHETIC_API void retro_reset() noexcept { reset_state(); }

extern "C" AYTHER_SYNTHETIC_API bool retro_load_game(const retro_game_info *const game) noexcept {
    if (game == nullptr || game->data == nullptr || game->size == 0)
        return false;
    rom_identity = 1469598103934665603ULL;
    const auto *bytes = static_cast<const std::uint8_t *>(game->data);
    for (std::size_t index{}; index < game->size; ++index) {
        rom_identity ^= bytes[index];
        rom_identity *= 1099511628211ULL;
    }
    reset_state();
    return true;
}

extern "C" AYTHER_SYNTHETIC_API void retro_unload_game() noexcept {
    rom_identity = 0;
    reset_state();
}

extern "C" AYTHER_SYNTHETIC_API void retro_run() noexcept {
    if (input_poll_callback != nullptr)
        input_poll_callback();
    std::uint16_t buttons{};
    if (input_state_callback != nullptr)
        for (unsigned button{}; button < 16; ++button)
            if (input_state_callback(0, 1, 0, button) != 0)
                buttons |= static_cast<std::uint16_t>(1U << button);
    state.last_input = buttons;
    audio_write_count = 0;
    if ((buttons & 0x0001U) != 0) {
        audio_writes = {{{100, 0x00A0, 0x55, 0}, {120, 0x00A4, 0x22, 0}, {140, 0x0028, 0xF0, 0}}};
        audio_write_count = 3;
    } else if ((buttons & 0x0002U) != 0) {
        audio_writes[0] = {100, 0x0028, 0x00, 0};
        audio_write_count = 1;
    }
    state.accumulator =
        state.accumulator * 6364136223846793005ULL + 1442695040888963407ULL + buttons;
    ++state.frame;
    pixels.fill(static_cast<std::uint32_t>(state.accumulator));
    if (video_callback != nullptr)
        video_callback(pixels.data(), 2, 2, 2 * sizeof(std::uint32_t));
    const auto sample = static_cast<std::int16_t>(state.accumulator >> 32U);
    audio = {sample, static_cast<std::int16_t>(-sample), sample,
             static_cast<std::int16_t>(-sample)};
    if (audio_batch_callback != nullptr)
        (void)audio_batch_callback(audio.data(), 2);
    else if (audio_callback != nullptr) {
        audio_callback(audio[0], audio[1]);
        audio_callback(audio[2], audio[3]);
    }
    (void)environment_callback;
}

extern "C" AYTHER_SYNTHETIC_API std::size_t retro_serialize_size() noexcept {
    return sizeof(state);
}

extern "C" AYTHER_SYNTHETIC_API bool retro_serialize(void *const data,
                                                     const std::size_t size) noexcept {
    if (data == nullptr || size != sizeof(state))
        return false;
    std::memcpy(data, &state, sizeof(state));
    return true;
}

extern "C" AYTHER_SYNTHETIC_API bool retro_unserialize(const void *const data,
                                                       const std::size_t size) noexcept {
    if (data == nullptr || size != sizeof(state))
        return false;
    SyntheticSerializedState candidate;
    std::memcpy(&candidate, data, sizeof(candidate));
    if (candidate.magic != synthetic_state_magic || candidate.version != synthetic_state_version ||
        candidate.reserved != 0)
        return false;
    state = candidate;
    return true;
}

extern "C" AYTHER_SYNTHETIC_API unsigned retro_get_region() noexcept { return 0; }
extern "C" AYTHER_SYNTHETIC_API void *retro_get_memory_data(const unsigned id) noexcept {
    if (id == synthetic_audio_writes_region)
        return audio_writes.data();
    if (id == synthetic_audio_count_region)
        return &audio_write_count;
    if (id == synthetic_audio_mute_region)
        return &audio_mute;
    return nullptr;
}
extern "C" AYTHER_SYNTHETIC_API std::size_t retro_get_memory_size(const unsigned id) noexcept {
    if (id == synthetic_audio_writes_region)
        return sizeof(audio_writes);
    if (id == synthetic_audio_count_region)
        return sizeof(audio_write_count);
    if (id == synthetic_audio_mute_region)
        return sizeof(audio_mute);
    return 0;
}
extern "C" AYTHER_SYNTHETIC_API void retro_cheat_reset() noexcept {}
extern "C" AYTHER_SYNTHETIC_API void retro_cheat_set(unsigned, bool, const char *) noexcept {}
