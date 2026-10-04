// Spec 002, BR-071 (RNF-7): every message of the supervisor exists in Spanish and in
// English: the general messages, each issue found before admission, each take diagnostic
// and the summary. The Spanish text is written with its accents.
#include "check_messages.h"
#include "check_options.h"

#include <array>
#include <iostream>
#include <string>
#include <string_view>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

// Words that, without their accent, are misspelled in Spanish.
// Whole words, compared after splitting on spaces and punctuation.
constexpr std::array unaccented{
    "mas",       "valido",   "valida",  "validos",      "validas",   "comprobacion",
    "sesion",    "tecnico",  "indice",  "encontro",     "limites",   "interrupcion",
    "ejecucion", "aun",      "version", "duracion",     "codigo",    "opcion",
    "condicion", "catalogo", "numero",  "presentacion", "cabecera_", "minima"};
// Phrases where `esta` must be the verb `está`.
constexpr std::array unaccented_phrases{"no esta ",         "esta vacio",    "esta disponible",
                                        "esta escribiendo", "esta repetida", "esta firmado"};

void check_spanish(std::string_view text, std::string_view key) {
    std::string lowered{text};
    for (auto &character : lowered)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    const auto fail = [&] {
        expect(false, std::string{"RNF-7: Spanish spelling of "} + std::string{key} + ": " +
                          std::string{text});
    };
    for (const auto phrase : unaccented_phrases)
        if (lowered.find(phrase) != std::string::npos)
            fail();
    std::string word;
    for (const char character : lowered + " ") {
        if ((character >= 'a' && character <= 'z') ||
            static_cast<unsigned char>(character) >= 0x80) {
            word.push_back(character);
            continue;
        }
        for (const auto wrong : unaccented)
            if (word == wrong)
                fail();
        word.clear();
    }
}

void general_messages() {
    for (const auto message : qa::all_check_messages()) {
        const auto spanish = qa::check_message(qa::CheckLanguage::spanish, message);
        const auto english = qa::check_message(qa::CheckLanguage::english, message);
        expect(!spanish.empty() && !english.empty() && spanish != english,
               "RNF-7: every general message exists in both languages");
        check_spanish(spanish, "general message");
    }
}

// Every code the CLI and run_check write in `audio_qa_error` or as a take diagnostic.
constexpr std::array codes{
    // Parser.
    "unknown_option", "missing_option_value", "empty_option_value", "option_value_too_long",
    "duplicate_option", "invalid_number", "missing_required_option", "unsupported_language",
    "unsupported_presentation", "unsupported_pack_mode", "unsupported_shaders",
    "invalid_unsigned_value", "malformed_core_option", "too_many_takes", "too_many_values",
    "too_many_arguments",
    // Auxiliary sources and effective values.
    "play_manifest_unavailable", "reference_unavailable", "play_manifest_rom_mismatch",
    "reference_rom_mismatch", "core_unresolved", "invalid_shaders_condition",
    // Materials.
    "material_not_found", "material_is_directory", "material_is_link", "material_not_regular",
    "material_in_use", "material_too_large", "material_unreadable", "material_changed",
    "material_unavailable",
    // Takes.
    "take_empty", "take_truncated_header", "take_invalid_magic", "take_unsupported_version",
    "take_too_large", "take_truncated", "take_invalid_frame_count", "take_empty_initial_state",
    "take_initial_state_too_large", "take_empty_compressed_state", "take_too_long",
    "take_game_mismatch",
    // Core and Runtime.
    "core_invalid", "core_load_failed", "core_platform_mismatch", "core_rejects_rom",
    "core_timing_unknown", "runtime_probe_failed", "runtime_rom_probe_unsupported",
    "runtime_identity_unavailable", "runtime_missing_protocol_1_1",
    "runtime_missing_visible_replay_v1", "runtime_missing_inspection_v1", "runtime_incompatible",
    // Pack.
    "pack_invalid", "pack_trust_unverified", "pack_untrusted", "pack_open_failed",
    "pack_catalog_invalid", "pack_assets_unreadable", "pack_probe_unavailable",
    "pack_game_mismatch",
    // Takes that ran or did not start.
    "replay_evidence_complete", "replay_cancelled", "not_started_after_cancellation",
    "not_started_after_failure", "evidence_reopen_failed", "game_state_restore_failed",
    "recording_duration_too_long",
    // Request.
    "request_identity_conflict", "session_busy", "request_summary_unreadable",
    "request_summary_publish_failed"};

void issue_messages() {
    for (const auto *code : codes) {
        const auto spanish = qa::check_issue_message(qa::CheckLanguage::spanish, code);
        const auto english = qa::check_issue_message(qa::CheckLanguage::english, code);
        expect(spanish && english && !spanish->empty() && !english->empty() && *spanish != *english,
               std::string{"RNF-7: the issue "} + code + " has both languages");
        if (spanish)
            check_spanish(*spanish, code);
    }
    expect(!qa::check_issue_message(qa::CheckLanguage::spanish, "not_a_code"),
           "an unknown code has no invented message");
}

void language_selection() {
    const qa::CheckOptions defaults;
    expect(defaults.language == "es" &&
               qa::parse_check_language(defaults.language) == qa::CheckLanguage::spanish &&
               qa::parse_check_language("en") == qa::CheckLanguage::english &&
               !qa::parse_check_language("fr") &&
               qa::check_language_code(qa::CheckLanguage::spanish) == "es" &&
               qa::check_language_code(qa::CheckLanguage::english) == "en",
           "RNF-7: Spanish by default, English available");
}

} // namespace

int main() {
    general_messages();
    issue_messages();
    language_selection();
    if (failures != 0)
        return 1;
    std::cout << "supervisor messages exist in Spanish and English\n";
    return 0;
}
