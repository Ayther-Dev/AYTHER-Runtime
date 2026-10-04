#include "launcher_messages.h"

#include <array>

namespace ayther::replay_qa_launcher {
namespace {

struct Text {
    LauncherText key;
    std::string_view spanish;
    std::string_view english;
};

constexpr std::array texts{
    Text{LauncherText::window_title, "AYTHER — Comprobación de replays", "AYTHER — Replay check"},
    Text{LauncherText::start, "Iniciar", "Start"},
    Text{LauncherText::cancel, "Cancelar", "Cancel"},
    Text{LauncherText::remove_pack, "Quitar", "Remove"},
    Text{LauncherText::choose_file, "Elegir…", "Choose…"},
    Text{LauncherText::add_take, "Añadir toma", "Add take"},
    Text{LauncherText::remove_take, "Quitar toma", "Remove take"},
    Text{LauncherText::move_up, "Subir", "Move up"},
    Text{LauncherText::move_down, "Bajar", "Move down"},
    Text{LauncherText::repeat_take, "Repetir", "Repeat"},
    Text{LauncherText::no_pack, "Sin pack", "No pack"},
    Text{LauncherText::no_takes, "Ninguna toma seleccionada", "No take selected"},
    Text{LauncherText::summary_title, "Valores efectivos", "Effective values"},
    Text{LauncherText::issues_title, "Errores e incompatibilidades",
         "Errors and incompatibilities"},
    Text{LauncherText::results_title, "Resultados", "Results"},
    Text{LauncherText::required_missing, "Campo obligatorio", "Required field"},
    Text{LauncherText::phase_idle, "Lista", "Ready"},
    Text{LauncherText::phase_validating, "Validando", "Validating"},
    Text{LauncherText::phase_admitted, "Admitida", "Admitted"},
    Text{LauncherText::phase_preparing, "Preparando la toma", "Preparing the take"},
    Text{LauncherText::phase_running, "Reproduciendo la toma", "Playing the take"},
    Text{LauncherText::phase_closing, "Cerrando", "Closing"},
    Text{LauncherText::phase_closed, "Terminada", "Finished"},
    Text{LauncherText::current_take, "Toma en curso", "Current take"},
    Text{LauncherText::playback, "Reproducción", "Playback"},
    Text{LauncherText::traversal, "Recorrido", "Traversal"},
    Text{LauncherText::evidence, "Evidencia", "Evidence"},
    Text{LauncherText::playback_natural_end, "Fin natural", "Natural end"},
    Text{LauncherText::playback_cancelled, "Cancelada", "Cancelled"},
    Text{LauncherText::playback_failed, "Fallida", "Failed"},
    Text{LauncherText::playback_interrupted, "Interrumpida", "Interrupted"},
    Text{LauncherText::playback_not_started, "No iniciada", "Not started"},
    Text{LauncherText::traversal_linear, "Lineal", "Linear"},
    Text{LauncherText::traversal_inspection, "Con inspección", "With inspection"},
    Text{LauncherText::traversal_post_end_inspection, "Inspección posterior",
         "Post-end inspection"},
    Text{LauncherText::traversal_unknown, "Desconocido", "Unknown"},
    Text{LauncherText::evidence_complete, "Completa", "Complete"},
    Text{LauncherText::evidence_incomplete, "Incompleta", "Incomplete"},
    Text{LauncherText::joint_linear_complete,
         "Todas las tomas se reprodujeron de forma lineal y completa",
         "Every take was played linearly and completely"},
    Text{LauncherText::joint_not_linear_complete,
         "La solicitud no se reprodujo entera de forma lineal",
         "The request was not played entirely and linearly"},
    Text{LauncherText::not_a_visual_evaluation, "Un resultado técnico no es una evaluación visual",
         "A technical result is not a visual evaluation"},
};

struct FieldText {
    std::string_view field;
    std::string_view spanish;
    std::string_view english;
};

constexpr std::array field_texts{
    FieldText{"runtime", "Runtime", "Runtime executable"},
    FieldText{"rom", "ROM", "ROM file"},
    FieldText{"core", "Core", "Libretro core"},
    FieldText{"reference", "Referencia", "Reference"},
    FieldText{"play_manifest", "Manifiesto de Play", "Play manifest"},
    FieldText{"pack", "Pack", "HD pack"},
    FieldText{"output", "Destino de la evidencia", "Evidence destination"},
    FieldText{"request_id", "Identidad de la solicitud", "Request identity"},
    FieldText{"language", "Idioma", "Language"},
    FieldText{"trust_registry", "Registro de confianza", "Trust registry"},
    FieldText{"presentation", "Presentación", "Presentation"},
    FieldText{"pack_mode", "Modo del pack", "Pack mode"},
    FieldText{"takes", "Tomas", "Takes"},
    FieldText{"profile", "Perfil", "Profile"},
    FieldText{"subsystems", "Subsistemas", "Subsystems"},
    FieldText{"mute_buses", "Buses silenciados", "Muted buses"},
    FieldText{"video_output", "Salida de vídeo", "Video output"},
    FieldText{"patch", "Parche", "Patch"},
    FieldText{"shaders", "Shaders de presentación", "Presentation shaders"},
    FieldText{"core_options", "Opciones del core", "Core options"},
};

constexpr auto keys = [] {
    std::array<LauncherText, texts.size()> result{};
    for (std::size_t index = 0; index < texts.size(); ++index)
        result[index] = texts[index].key;
    return result;
}();

} // namespace

std::span<const LauncherText> all_launcher_texts() noexcept { return keys; }

std::string_view launcher_text(LauncherLanguage language, LauncherText text) noexcept {
    for (const auto &entry : texts)
        if (entry.key == text)
            return language == LauncherLanguage::english ? entry.english : entry.spanish;
    return {};
}

std::string_view field_label(LauncherLanguage language, std::string_view launcher_field) noexcept {
    for (const auto &entry : field_texts)
        if (entry.field == launcher_field)
            return language == LauncherLanguage::english ? entry.english : entry.spanish;
    return {};
}

std::string_view section_title(LauncherLanguage language,
                               audio_qa::CheckOptionCategory category) noexcept {
    const bool english = language == LauncherLanguage::english;
    switch (category) {
    case audio_qa::CheckOptionCategory::rom:
        return english ? "Game" : "Juego";
    case audio_qa::CheckOptionCategory::take:
        return english ? "Takes" : "Tomas";
    case audio_qa::CheckOptionCategory::pack:
        return english ? "HD pack" : "Pack";
    case audio_qa::CheckOptionCategory::environment:
        return english ? "Environment and conditions" : "Entorno y condiciones";
    case audio_qa::CheckOptionCategory::auxiliary:
        return english ? "Auxiliary information" : "Información auxiliar";
    case audio_qa::CheckOptionCategory::destination:
        return english ? "Destination and identity" : "Destino e identidad";
    case audio_qa::CheckOptionCategory::presentation:
        return english ? "Presentation" : "Presentación";
    case audio_qa::CheckOptionCategory::language:
        return english ? "Language" : "Idioma";
    }
    return {};
}

std::string_view source_text(LauncherLanguage language, audio_qa::ValueSource source) noexcept {
    const bool english = language == LauncherLanguage::english;
    switch (source) {
    case audio_qa::ValueSource::explicit_option:
        return english ? "selected" : "seleccionado";
    case audio_qa::ValueSource::play_manifest:
        return english ? "Play manifest" : "manifiesto de Play";
    case audio_qa::ValueSource::reference:
        return english ? "reference" : "referencia";
    case audio_qa::ValueSource::environment:
        return english ? "environment" : "entorno";
    case audio_qa::ValueSource::default_value:
        return english ? "default" : "por defecto";
    case audio_qa::ValueSource::generated:
        return english ? "generated" : "generado";
    }
    return {};
}

LauncherLanguage parse_launcher_language(std::string_view code) noexcept {
    return code == "en" ? LauncherLanguage::english : LauncherLanguage::spanish;
}

} // namespace ayther::replay_qa_launcher
