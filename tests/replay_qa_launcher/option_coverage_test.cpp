// Spec 002:
//   BR-167 (RF-1.5) the sections of the launcher show every option of the table once, each
//          with the validation of its kind.
//   BR-171 (RF-1.5, RF-1.8) every descriptor has a launcher field, and the correspondence
//          with the beta.8 inventory is regenerated with no reference option left without
//          a correspondence. Arguments: the reference inventory and the output Markdown.
#include "check_option_descriptors.h"
#include "launcher_model.h"
#include "launcher_view.h"
#include "option_correspondence.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <variant>

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

void sections_cover_the_table() {
    const auto sections = la::launcher_sections();
    for (const auto &descriptor : qa::check_option_descriptors()) {
        std::size_t found = 0;
        for (const auto &section : sections)
            for (const auto &field : section.fields)
                if (field.flag == descriptor.flag) {
                    ++found;
                    expect(field.input == la::input_for(descriptor.kind) &&
                               section.category == descriptor.category,
                           std::string{"RF-1.5: "} + std::string{descriptor.flag} +
                               " is drawn in its section with the input of its kind");
                }
        expect(found == 1U, std::string{"RF-1.5: "} + std::string{descriptor.flag} +
                                " appears exactly once in the launcher");
    }
    la::LauncherModel model;
    for (const auto &descriptor : qa::check_option_descriptors())
        expect(std::any_of(model.fields().begin(), model.fields().end(),
                           [&descriptor](const la::ModelField &field) {
                               return field.descriptor->flag == descriptor.flag &&
                                      !descriptor.launcher_field.empty();
                           }),
               std::string{"RF-1.8: "} + std::string{descriptor.flag} + " has a launcher field");
}

void correspondence(const std::filesystem::path &inventory, const std::filesystem::path &output) {
    std::ifstream input{inventory, std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    const auto generated = la::format_option_correspondence(text);
    const auto *markdown = std::get_if<std::string>(&generated);
    expect(markdown != nullptr, "RF-1.8: every reference option has a correspondence");
    if (markdown == nullptr) {
        std::cerr << "  missing: " << std::get<la::CorrespondenceError>(generated).flag << '\n';
        return;
    }
    expect(markdown->find("| `--pack` | obligatoria |") != std::string::npos &&
               markdown->find("| `--rom` | — |") != std::string::npos &&
               markdown->find("RF-1.3") != std::string::npos,
           "RF-1.8: changed and added options cite their requirements");
    std::filesystem::create_directories(output.parent_path());
    std::ofstream{output, std::ios::binary} << *markdown;
    const auto broken = la::format_option_correspondence(
        "schema = 1\n[[option]]\ncommand = \"check\"\nflag = \"--mystery\"\nrequired = true\n"
        "repeatable = false\n");
    expect(std::holds_alternative<la::CorrespondenceError>(broken) &&
               std::get<la::CorrespondenceError>(broken).flag == "--mystery",
           "RF-1.8: a reference option without correspondence fails");
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: option_coverage_test <reference-inventory.toml> <output.md>\n";
        return 2;
    }
    sections_cover_the_table();
    correspondence(argv[1], argv[2]);
    if (failures != 0)
        return 1;
    std::cout << "every option has its launcher field and its correspondence\n";
    return 0;
}
