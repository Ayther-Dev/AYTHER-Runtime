#include "replay_messages.h"

#include <array>

namespace ayther::replay_inspection {
namespace {

struct Text {
    ReplayMessage key;
    std::string_view spanish;
    std::string_view english;
};

constexpr std::array texts{
    Text{ReplayMessage::phase_preparing, "Preparando", "Preparing"},
    Text{ReplayMessage::phase_playing, "Reproduciendo", "Playing"},
    Text{ReplayMessage::phase_pausing, "Pausando", "Pausing"},
    Text{ReplayMessage::phase_paused, "En pausa", "Paused"},
    Text{ReplayMessage::phase_recovering, "Recuperando la posición", "Recovering the position"},
    Text{ReplayMessage::phase_interrupted, "Interrumpido", "Interrupted"},
    Text{ReplayMessage::phase_ended, "Fin de la toma", "End of the take"},
    Text{ReplayMessage::phase_closing, "Cerrando", "Closing"},
    Text{ReplayMessage::busy, "Ocupado", "Busy"},
    Text{ReplayMessage::not_available, "No disponible", "Not available"},
    Text{ReplayMessage::no_next_frame, "Sin frame siguiente", "No next frame"},
    Text{ReplayMessage::recovery_failed, "No se pudo recuperar ese frame; se mantiene la posición",
         "That frame could not be recovered; the position is kept"},
    Text{ReplayMessage::recovering, "Recuperando…", "Recovering…"},
    Text{ReplayMessage::cause_video_lost, "Se perdió la imagen", "The image was lost"},
    Text{ReplayMessage::cause_audio_device_removed, "Se retiró el dispositivo de audio",
         "The audio device was removed"},
    Text{ReplayMessage::cause_window_minimized, "La ventana está minimizada",
         "The window is minimized"},
    Text{ReplayMessage::cause_device_lost,
         "Se perdió el dispositivo gráfico; cancele desde el launcher",
         "The graphics device was lost; cancel from the launcher"},
    Text{ReplayMessage::cause_unknown, "Presentación interrumpida", "Presentation interrupted"},
    Text{ReplayMessage::no_pack, "Sin pack", "No pack"},
    Text{ReplayMessage::label_rom, "ROM", "ROM file"},
    Text{ReplayMessage::label_take, "Toma", "Take"},
    Text{ReplayMessage::label_pack, "Pack", "HD pack"},
    Text{ReplayMessage::label_phase, "Fase", "Phase"},
    Text{ReplayMessage::label_frame, "Frame", "Frame number"},
    Text{ReplayMessage::label_total_frames, "Frames totales", "Total frames"},
    Text{ReplayMessage::label_elapsed, "Transcurrido (ms)", "Elapsed (ms)"},
    Text{ReplayMessage::label_remaining, "Restante (ms)", "Remaining (ms)"},
    Text{ReplayMessage::label_processing, "Procesamiento (ms)", "Processing (ms)"},
    Text{ReplayMessage::label_fps, "Instantáneos (FPS)", "Instantaneous (FPS)"},
    Text{ReplayMessage::label_occurrences, "Ocurrencias", "Occurrences"},
    Text{ReplayMessage::label_overflow, "Filas no mostradas", "Rows not shown"},
    Text{ReplayMessage::debug_hidden_hint, "Pulse I para la depuración", "Press I for debug"},
};

constexpr auto keys = [] {
    std::array<ReplayMessage, texts.size()> result{};
    for (std::size_t index = 0; index < texts.size(); ++index)
        result[index] = texts[index].key;
    return result;
}();

} // namespace

std::span<const ReplayMessage> all_replay_messages() noexcept { return keys; }

std::string_view replay_message(ReplayLanguage language, ReplayMessage message) noexcept {
    for (const auto &text : texts)
        if (text.key == message)
            return language == ReplayLanguage::english ? text.english : text.spanish;
    return {};
}

ReplayLanguage parse_replay_language(std::string_view code) noexcept {
    return code == "en" ? ReplayLanguage::english : ReplayLanguage::spanish;
}

ReplayMessage interruption_cause_message(std::string_view cause) noexcept {
    if (cause == "video_acquire_failed" || cause == "video_present_failed")
        return ReplayMessage::cause_video_lost;
    if (cause == "audio_device_removed")
        return ReplayMessage::cause_audio_device_removed;
    if (cause == "window_minimized")
        return ReplayMessage::cause_window_minimized;
    if (cause == "device_lost")
        return ReplayMessage::cause_device_lost;
    return ReplayMessage::cause_unknown;
}

} // namespace ayther::replay_inspection
