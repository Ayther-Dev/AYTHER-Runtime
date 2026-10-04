// Spec 002, BR-158 (RNF-7; plan §5.14): every text of the launcher exists in Spanish, the
// default, and in English: one label per option of the table, the sections, the buttons,
// the phases, the origins of the effective values and the results.
#include "check_option_descriptors.h"
#include "launcher_messages.h"

#include <iostream>
#include <string>
#include <string_view>

namespace la = ayther::replay_qa_launcher;
namespace qa = ayther::audio_qa;

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
    for (const auto key : la::all_launcher_texts()) {
        const auto spanish = la::launcher_text(la::LauncherLanguage::spanish, key);
        const auto english = la::launcher_text(la::LauncherLanguage::english, key);
        expect(!spanish.empty() && !english.empty() && spanish != english,
               "RNF-7: every launcher text exists in both languages");
    }
    for (const auto &descriptor : qa::check_option_descriptors()) {
        const auto spanish =
            la::field_label(la::LauncherLanguage::spanish, descriptor.launcher_field);
        const auto english =
            la::field_label(la::LauncherLanguage::english, descriptor.launcher_field);
        expect(!spanish.empty() && !english.empty() && spanish != english,
               std::string{"RNF-7: the option "} + std::string{descriptor.flag} +
                   " has a label in both languages");
    }
    for (const auto category :
         {qa::CheckOptionCategory::rom, qa::CheckOptionCategory::take,
          qa::CheckOptionCategory::pack, qa::CheckOptionCategory::environment,
          qa::CheckOptionCategory::auxiliary, qa::CheckOptionCategory::destination,
          qa::CheckOptionCategory::presentation, qa::CheckOptionCategory::language})
        expect(!la::section_title(la::LauncherLanguage::spanish, category).empty() &&
                   la::section_title(la::LauncherLanguage::spanish, category) !=
                       la::section_title(la::LauncherLanguage::english, category),
               "plan §5.14: every section has a title in both languages");
    using T = la::LauncherText;
    const auto es = la::LauncherLanguage::spanish;
    expect(la::launcher_text(es, T::start) == "Iniciar" &&
               la::launcher_text(es, T::cancel) == "Cancelar" &&
               la::launcher_text(es, T::remove_pack) == "Quitar" &&
               la::launcher_text(es, T::no_pack) == "Sin pack",
           "RF-1.3, RF-2.3: Iniciar, Cancelar, Quitar and «Sin pack»");
    expect(la::source_text(es, qa::ValueSource::environment) == "entorno" &&
               la::source_text(la::LauncherLanguage::english, qa::ValueSource::environment) ==
                   "environment",
           "RF-1.6: every origin of an effective value is named");
    expect(la::parse_launcher_language("en") == la::LauncherLanguage::english &&
               la::parse_launcher_language("") == es,
           "RNF-7: Spanish by default");
    if (failures != 0)
        return 1;
    std::cout << "launcher texts exist in Spanish and English\n";
    return 0;
}
