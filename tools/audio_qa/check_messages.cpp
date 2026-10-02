#include "check_messages.h"

namespace ayther::audio_qa {

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

std::string_view check_message(CheckLanguage language, CheckMessage message) noexcept {
    const bool english = language == CheckLanguage::english;
    switch (message) {
    case CheckMessage::invalid_invocation:
        return english ? "Run the check command with its required options."
                       : "Ejecute el comando check con sus opciones obligatorias.";
    case CheckMessage::invalid_options:
        return english ? "One or more command options are invalid."
                       : "Una o mas opciones del comando no son validas.";
    case CheckMessage::missing_profile_primary_take:
        return english ? "The profile does not define a primary recording."
                       : "El perfil no define una toma principal.";
    case CheckMessage::output_root_unavailable:
        return english ? "The evidence output directory is unavailable."
                       : "El directorio de salida de evidencia no esta disponible.";
    case CheckMessage::request_ledger_unavailable:
        return english ? "The request record cannot be opened."
                       : "No se puede abrir el registro de solicitudes.";
    case CheckMessage::request_ledger_inconsistent:
        return english ? "The request record contains multiple active runs."
                       : "El registro de solicitudes contiene varias ejecuciones "
                         "activas.";
    case CheckMessage::request_ledger_publish_failed:
        return english ? "The request could not be preserved."
                       : "No se pudo conservar la solicitud.";
    case CheckMessage::request_accepted:
        return english ? "The request was accepted." : "La solicitud fue aceptada.";
    case CheckMessage::request_known:
        return english ? "The request was already registered."
                       : "La solicitud ya estaba registrada.";
    case CheckMessage::request_identity_conflict:
        return english ? "The request identifier belongs to different content."
                       : "El identificador de solicitud pertenece a otro contenido.";
    case CheckMessage::session_busy:
        return english ? "Another check currently occupies the session."
                       : "Otra comprobacion ocupa la sesion.";
    case CheckMessage::invalid_request:
        return english ? "The check request is invalid."
                       : "La solicitud de comprobacion no es valida.";
    case CheckMessage::request_capacity_exceeded:
        return english ? "The request exceeds the supported limits."
                       : "La solicitud supera los limites admitidos.";
    case CheckMessage::interrupt_handler_unavailable:
        return english ? "Controlled interruption is unavailable."
                       : "La interrupcion controlada no esta disponible.";
    case CheckMessage::check_execution_unavailable:
        return english ? "Check execution is not available yet."
                       : "La ejecucion de la comprobacion aun no esta disponible.";
    case CheckMessage::technical_summary_invalid:
        return english ? "A valid technical summary could not be produced."
                       : "No se pudo generar un resumen tecnico valido.";
    case CheckMessage::query_index_unavailable:
        return english ? "The evidence query index is unavailable."
                       : "El indice de consulta de evidencia no esta disponible.";
    case CheckMessage::query_found:
        return english ? "Recorded routes from the event to audio were found."
                       : "Se encontraron recorridos registrados del evento al audio.";
    case CheckMessage::query_not_found:
        return english ? "No recorded route from the event to audio was found."
                       : "No se encontro un recorrido registrado del evento al audio.";
    }
    return english ? "The check could not continue." : "La comprobacion no pudo continuar.";
}

} // namespace ayther::audio_qa
