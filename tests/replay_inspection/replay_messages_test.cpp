// Spec 002, BR-133 (RNF-7, RNF-4): the texts of the replay window in Spanish (default) and
// English: phases, «ocupado», «no disponible», «sin frame siguiente», interruption causes,
// «Sin pack» and labels with their units. Every key must have both languages.
#include "replay_messages.h"

#include <iostream>
#include <string>
#include <string_view>

namespace ri = ayther::replay_inspection;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

} // namespace

int main() {
    for (const auto key : ri::all_replay_messages()) {
        const auto spanish = ri::replay_message(ri::ReplayLanguage::spanish, key);
        const auto english = ri::replay_message(ri::ReplayLanguage::english, key);
        expect(!spanish.empty() && !english.empty() && spanish != english,
               "RNF-7: every window text exists in both languages");
    }
    using M = ri::ReplayMessage;
    const auto es = ri::ReplayLanguage::spanish;
    const auto en = ri::ReplayLanguage::english;
    expect(ri::replay_message(es, M::no_pack) == "Sin pack" &&
               ri::replay_message(en, M::no_pack) == "No pack",
           "RF-1.3: «Sin pack»");
    expect(ri::replay_message(es, M::busy) == "Ocupado" &&
               ri::replay_message(es, M::not_available) == "No disponible" &&
               ri::replay_message(es, M::no_next_frame) == "Sin frame siguiente",
           "RNF-7: the notices of the controller");
    expect(ri::replay_message(es, M::phase_paused) == "En pausa" &&
               ri::replay_message(en, M::phase_paused) == "Paused",
           "RNF-7: the phases");
    expect(ri::replay_message(es, M::cause_audio_device_removed).find("audio") != std::string::npos,
           "RF-2.6: interruption causes are explained");
    expect(ri::replay_message(es, M::label_elapsed).find("(ms)") != std::string::npos &&
               ri::replay_message(en, M::label_fps).find("(FPS)") != std::string::npos,
           "RNF-4: labels carry their units");
    expect(ri::interruption_cause_message("video_acquire_failed") == M::cause_video_lost &&
               ri::interruption_cause_message("window_minimized") == M::cause_window_minimized &&
               ri::interruption_cause_message("device_lost") == M::cause_device_lost &&
               ri::interruption_cause_message("something_else") == M::cause_unknown,
           "RF-2.6: every cause of presentation_health has a message");
    expect(ri::parse_replay_language("en") == en && ri::parse_replay_language("es") == es &&
               ri::parse_replay_language("fr") == es,
           "RNF-7: Spanish by default");
    if (failures != 0)
        return 1;
    std::cout << "window texts exist in Spanish and English\n";
    return 0;
}
