#include "check_messages.h"

#include <algorithm>
#include <array>

namespace ayther::audio_qa {

namespace {

struct IssueText {
    std::string_view code;
    std::string_view spanish;
    std::string_view english;
};

// Spec 002 (RNF-7): one entry per code written in `audio_qa_error` or as a take diagnostic.
constexpr std::array issue_texts{
    // Parser.
    IssueText{"too_many_arguments", "La orden tiene demasiados argumentos.",
              "The command has too many arguments."},
    IssueText{"unknown_option", "La opción no existe.", "The option does not exist."},
    IssueText{"missing_option_value", "Falta el valor de la opción.",
              "The option is missing its value."},
    IssueText{"empty_option_value", "El valor de la opción está vacío.",
              "The option value is empty."},
    IssueText{"option_value_too_long", "El valor de la opción es demasiado largo.",
              "The option value is too long."},
    IssueText{"duplicate_option", "La opción está repetida.", "The option is repeated."},
    IssueText{"too_many_takes", "Hay más tomas de las admitidas.",
              "There are more takes than supported."},
    IssueText{"missing_required_option", "Falta una opción obligatoria.",
              "A required option is missing."},
    IssueText{"unsupported_language", "El idioma no es es ni en.", "The language is not es or en."},
    IssueText{"invalid_number", "El número no es válido.", "The number is not valid."},
    IssueText{"unsupported_presentation", "La presentación no es none ni visible.",
              "The presentation is not none or visible."},
    IssueText{"unsupported_pack_mode", "El modo de pack no es hd ni original.",
              "The pack mode is not hd or original."},
    IssueText{"unsupported_shaders", "Los shaders deben ser on u off.",
              "Shaders must be on or off."},
    IssueText{"invalid_unsigned_value", "El valor debe ser un entero sin signo de 32 bits.",
              "The value must be an unsigned 32-bit integer."},
    IssueText{"malformed_core_option", "La opción del core debe tener la forma clave=valor.",
              "The core option must have the form key=value."},
    IssueText{"too_many_values", "La opción se repite más veces de las admitidas.",
              "The option is repeated more times than supported."},
    // Auxiliary sources and effective values.
    IssueText{"play_manifest_unavailable", "No se puede leer el manifiesto de Play.",
              "The Play manifest cannot be read."},
    IssueText{"reference_unavailable", "No se puede leer la referencia.",
              "The reference cannot be read."},
    IssueText{"play_manifest_rom_mismatch", "El manifiesto de Play describe otra ROM.",
              "The Play manifest describes another ROM."},
    IssueText{"reference_rom_mismatch", "La referencia describe otra ROM.",
              "The reference describes another ROM."},
    IssueText{"core_unresolved", "No hay core: indíquelo o configúrelo en Play CE.",
              "There is no core: select one or configure it in Play CE."},
    IssueText{"invalid_shaders_condition", "La condición de shaders no es válida.",
              "The shaders condition is not valid."},
    // Materials.
    IssueText{"material_not_found", "El archivo no existe.", "The file does not exist."},
    IssueText{"material_is_directory", "Es una carpeta, no un archivo.",
              "It is a folder, not a file."},
    IssueText{"material_is_link", "Es un enlace; seleccione el archivo real.",
              "It is a link; select the real file."},
    IssueText{"material_not_regular", "No es un archivo normal.", "It is not a regular file."},
    IssueText{"material_in_use", "Otro programa está escribiendo el archivo.",
              "Another program is writing the file."},
    IssueText{"material_too_large", "El archivo supera el tamaño admitido.",
              "The file exceeds the supported size."},
    IssueText{"material_unreadable", "No se puede leer el archivo.", "The file cannot be read."},
    IssueText{"material_changed", "El archivo cambió después de validarlo; no se usó.",
              "The file changed after it was validated; it was not used."},
    IssueText{"material_unavailable", "El archivo dejó de estar disponible.",
              "The file is no longer available."},
    // Takes.
    IssueText{"take_empty", "La toma está vacía.", "The take is empty."},
    IssueText{"take_truncated_header", "La cabecera de la toma está incompleta.",
              "The take header is incomplete."},
    IssueText{"take_invalid_magic", "El archivo no es una toma ARP1.",
              "The file is not an ARP1 take."},
    IssueText{"take_unsupported_version", "La versión de la toma no está admitida (2 a 8).",
              "The take version is not supported (2 to 8)."},
    IssueText{"take_too_large", "La toma supera 64 MiB.", "The take exceeds 64 MiB."},
    IssueText{"take_truncated", "La toma está incompleta.", "The take is incomplete."},
    IssueText{"take_invalid_frame_count", "La toma debe tener de 1 a 54 000 frames.",
              "The take must have 1 to 54,000 frames."},
    IssueText{"take_empty_initial_state", "La toma no tiene estado inicial.",
              "The take has no initial state."},
    IssueText{"take_initial_state_too_large", "El estado inicial de la toma es demasiado grande.",
              "The initial state of the take is too large."},
    IssueText{"take_empty_compressed_state", "El estado comprimido de la toma está vacío.",
              "The compressed state of the take is empty."},
    IssueText{"take_initial_state_invalid",
              "El estado inicial de la toma está dañado: no se puede descomprimir entero.",
              "The initial state of the take is damaged: it cannot be decompressed whole."},
    IssueText{"take_initial_state_unreadable",
              "No hay memoria para comprobar el estado inicial de la toma.",
              "There is not enough memory to check the initial state of the take."},
    IssueText{"profile_not_in_pack", "El pack no ofrece el perfil pedido.",
              "The pack does not offer the requested profile."},
    IssueText{"take_too_long", "La toma dura más de 900 s con el timing de este core.",
              "The take lasts more than 900 s at the timing of this core."},
    IssueText{"take_game_mismatch", "La toma se grabó con otro juego.",
              "The take was recorded with another game."},
    // Core and Runtime.
    IssueText{"core_invalid", "La biblioteca no es un core libretro.",
              "The library is not a libretro core."},
    IssueText{"core_load_failed", "No se pudo cargar el core.", "The core could not be loaded."},
    IssueText{"core_platform_mismatch", "El core no admite la plataforma de la ROM.",
              "The core does not support the platform of the ROM."},
    IssueText{"core_rejects_rom", "El core no carga esta ROM.", "The core does not load this ROM."},
    IssueText{"core_timing_unknown", "El core no informa de su timing con esta ROM.",
              "The core does not report its timing with this ROM."},
    IssueText{"runtime_probe_failed", "No se pudo consultar el Runtime.",
              "The Runtime could not be queried."},
    IssueText{"runtime_rom_probe_unsupported", "El Runtime no puede sondear la ROM; actualícelo.",
              "The Runtime cannot probe the ROM; update it."},
    IssueText{"runtime_identity_unavailable", "No se pudo identificar el Runtime.",
              "The Runtime could not be identified."},
    IssueText{"runtime_missing_protocol_1_1",
              "La reproducción visible necesita un Runtime con el protocolo QA 1.1.",
              "Visible replay needs a Runtime with QA protocol 1.1."},
    IssueText{"runtime_missing_visible_replay_v1",
              "El Runtime no ofrece la reproducción visible (visible_replay_v1).",
              "The Runtime does not offer visible replay (visible_replay_v1)."},
    IssueText{"runtime_missing_inspection_v1",
              "El Runtime no ofrece la inspección (inspection_v1).",
              "The Runtime does not offer inspection (inspection_v1)."},
    IssueText{"runtime_incompatible", "El Runtime no es compatible con esta comprobación.",
              "The Runtime is not compatible with this check."},
    // Pack.
    IssueText{"pack_invalid", "El pack no es válido.", "The pack is not valid."},
    IssueText{"pack_trust_unverified",
              "El pack está firmado y falta el registro de confianza para verificarlo.",
              "The pack is signed and the trust registry to verify it is missing."},
    IssueText{"pack_untrusted", "El registro de confianza no avala este pack.",
              "The trust registry does not vouch for this pack."},
    IssueText{"trust_registry_invalid",
              "El registro de confianza no existe, no se puede leer o no es válido.",
              "The trust registry is missing, cannot be read or is not valid."},
    IssueText{"pack_open_failed", "No se pudo abrir el pack.", "The pack could not be opened."},
    IssueText{"pack_catalog_invalid", "El catálogo del pack no es válido.",
              "The pack catalog is not valid."},
    IssueText{"pack_assets_unreadable", "El pack tiene recursos que no se pueden decodificar.",
              "The pack has assets that cannot be decoded."},
    IssueText{"pack_probe_unavailable", "No se pudo sondear el pack; nunca se ignora.",
              "The pack could not be probed; it is never ignored."},
    IssueText{"pack_game_mismatch", "El pack es de otro juego.", "The pack is for another game."},
    // Takes that ran or did not start.
    IssueText{"replay_evidence_complete", "La toma terminó con la evidencia completa.",
              "The take ended with complete evidence."},
    IssueText{"replay_cancelled", "La toma se canceló.", "The take was cancelled."},
    IssueText{"not_started_after_cancellation", "No se inició: la solicitud se canceló.",
              "Not started: the request was cancelled."},
    IssueText{"not_started_after_failure", "No se inició: una toma anterior falló.",
              "Not started: an earlier take failed."},
    IssueText{"evidence_reopen_failed", "La evidencia no se pudo reabrir completa.",
              "The evidence could not be reopened complete."},
    IssueText{"game_state_restore_failed", "No se pudo restaurar el estado inicial de la toma.",
              "The initial state of the take could not be restored."},
    IssueText{"recording_duration_too_long", "La toma dura más de 900 s.",
              "The take lasts more than 900 s."},
    // Request.
    IssueText{"request_identity_conflict",
              "Ese identificador de solicitud ya se usó con otras selecciones.",
              "That request identifier was already used with other selections."},
    IssueText{"session_busy", "Otra comprobación ocupa el destino.",
              "Another check occupies the destination."},
    IssueText{"request_summary_unreadable", "No se puede leer el resumen confirmado.",
              "The confirmed summary cannot be read."},
    IssueText{"request_summary_publish_failed", "No se pudo conservar el resumen de la solicitud.",
              "The request summary could not be preserved."},
};

constexpr std::array check_messages{
    CheckMessage::invalid_invocation,
    CheckMessage::invalid_options,
    CheckMessage::missing_profile_primary_take,
    CheckMessage::output_root_unavailable,
    CheckMessage::request_ledger_unavailable,
    CheckMessage::request_ledger_inconsistent,
    CheckMessage::request_ledger_publish_failed,
    CheckMessage::request_accepted,
    CheckMessage::request_known,
    CheckMessage::request_identity_conflict,
    CheckMessage::session_busy,
    CheckMessage::invalid_request,
    CheckMessage::request_capacity_exceeded,
    CheckMessage::interrupt_handler_unavailable,
    CheckMessage::check_execution_unavailable,
    CheckMessage::technical_summary_invalid,
    CheckMessage::query_index_unavailable,
    CheckMessage::query_found,
    CheckMessage::query_not_found,
    CheckMessage::summary_complete,
    CheckMessage::summary_incomplete,
    CheckMessage::summary_cancelled,
    CheckMessage::summary_preservation_failure,
};

} // namespace

std::optional<CheckLanguage> parse_check_language(std::string_view value) noexcept {
    if (value == "es")
        return CheckLanguage::spanish;
    if (value == "en")
        return CheckLanguage::english;
    return std::nullopt;
}

std::string_view check_language_code(CheckLanguage language) noexcept {
    return language == CheckLanguage::english ? "en" : "es";
}

std::span<const CheckMessage> all_check_messages() noexcept { return check_messages; }

std::string_view check_message(CheckLanguage language, CheckMessage message) noexcept {
    const bool english = language == CheckLanguage::english;
    switch (message) {
    case CheckMessage::invalid_invocation:
        return english ? "Run the check command with its required options."
                       : "Ejecute la orden check con sus opciones obligatorias.";
    case CheckMessage::invalid_options:
        return english ? "One or more command options are invalid."
                       : "Una o más opciones de la orden no son válidas.";
    case CheckMessage::missing_profile_primary_take:
        return english ? "The profile does not define a primary recording."
                       : "El perfil no define una toma principal.";
    case CheckMessage::output_root_unavailable:
        return english ? "The evidence output directory is unavailable."
                       : "La carpeta de destino de la evidencia no está disponible.";
    case CheckMessage::request_ledger_unavailable:
        return english ? "The request record cannot be opened."
                       : "No se puede abrir el registro de solicitudes.";
    case CheckMessage::request_ledger_inconsistent:
        return english ? "The request record contains multiple active runs."
                       : "El registro de solicitudes contiene varias ejecuciones activas.";
    case CheckMessage::request_ledger_publish_failed:
        return english ? "The request could not be preserved."
                       : "No se pudo conservar la solicitud.";
    case CheckMessage::request_accepted:
        return english ? "The request was accepted." : "La solicitud fue aceptada.";
    case CheckMessage::request_known:
        return english ? "The request was already registered; this is its confirmed result."
                       : "La solicitud ya estaba registrada; este es su resultado confirmado.";
    case CheckMessage::request_identity_conflict:
        return english ? "The request identifier belongs to different selections."
                       : "El identificador de solicitud pertenece a otras selecciones.";
    case CheckMessage::session_busy:
        return english ? "Another check currently occupies the destination."
                       : "Otra comprobación ocupa el destino.";
    case CheckMessage::invalid_request:
        return english ? "The check request is invalid; see each field."
                       : "La solicitud de comprobación no es válida; vea cada campo.";
    case CheckMessage::request_capacity_exceeded:
        return english ? "The request exceeds the supported limits."
                       : "La solicitud supera los límites admitidos.";
    case CheckMessage::interrupt_handler_unavailable:
        return english ? "Controlled interruption is unavailable."
                       : "La interrupción controlada no está disponible.";
    case CheckMessage::check_execution_unavailable:
        return english ? "Check execution is not available yet."
                       : "La ejecución de la comprobación aún no está disponible.";
    case CheckMessage::technical_summary_invalid:
        return english ? "A valid technical summary could not be produced."
                       : "No se pudo generar un resumen técnico válido.";
    case CheckMessage::query_index_unavailable:
        return english ? "The evidence query index is unavailable."
                       : "El índice de consulta de evidencia no está disponible.";
    case CheckMessage::query_found:
        return english ? "Recorded routes from the event to audio were found."
                       : "Se encontraron recorridos registrados del evento al audio.";
    case CheckMessage::query_not_found:
        return english ? "No recorded route from the event to audio was found."
                       : "No se encontró un recorrido registrado del evento al audio.";
    case CheckMessage::summary_complete:
        return english ? "Every take ended naturally with complete evidence. This is not a "
                         "visual evaluation."
                       : "Todas las tomas terminaron de forma natural con la evidencia completa. "
                         "No es una evaluación visual.";
    case CheckMessage::summary_incomplete:
        return english ? "Some take failed, did not start or kept incomplete evidence."
                       : "Alguna toma falló, no se inició o tiene la evidencia incompleta.";
    case CheckMessage::summary_cancelled:
        return english ? "The request was cancelled; the pending takes did not start."
                       : "La solicitud se canceló; las tomas pendientes no se iniciaron.";
    case CheckMessage::summary_preservation_failure:
        return english ? "The minimum evidence could not be preserved."
                       : "No se pudo conservar la evidencia mínima.";
    }
    return english ? "The check could not continue." : "La comprobación no pudo continuar.";
}

std::optional<std::string_view> check_issue_message(CheckLanguage language,
                                                    std::string_view code) noexcept {
    const auto found = std::find_if(issue_texts.begin(), issue_texts.end(),
                                    [code](const IssueText &text) { return text.code == code; });
    if (found == issue_texts.end())
        return std::nullopt;
    return language == CheckLanguage::english ? found->english : found->spanish;
}

} // namespace ayther::audio_qa
