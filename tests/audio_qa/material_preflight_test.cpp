// Spec 002: RF-2.2 and RF-2.11 (plan §5.2). Each material is pinned by SHA-256, size and
// file identity, and anything that is not a readable regular file is rejected in its field.
#include "content_hash.h"
#include "material_preflight.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

void write_file(const std::filesystem::path &path, std::string_view bytes) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

qa::ContentIdentity hash(std::string_view bytes) {
    return qa::identify_content(std::as_bytes(std::span{bytes.data(), bytes.size()}));
}

const qa::MaterialPin *pinned(const qa::MaterialPinResult &result) {
    return std::get_if<qa::MaterialPin>(&result);
}

bool rejected(const qa::MaterialPinResult &result, std::string_view field,
              qa::MaterialPinError error) {
    const auto *issue = std::get_if<qa::MaterialIssue>(&result);
    return issue != nullptr && issue->field == field && issue->error == error;
}

void regular_file_is_pinned(const std::filesystem::path &root) {
    const auto path = root / "game.md";
    write_file(path, "rom-content");
    const auto first = qa::pin_material(qa::MaterialPinRole::take, "--take", path, 2);
    const auto *pin = pinned(first);
    expect(pin != nullptr, "RF-2.2: a regular file is pinned");
    if (pin == nullptr)
        return;
    expect(pin->content == hash("rom-content") && pin->content.byte_size == 11U,
           "RF-2.2: the pin holds the SHA-256 and the size of the content");
    expect(pin->role == qa::MaterialPinRole::take && pin->field == "--take" && pin->position == 2 &&
               pin->path == path,
           "RF-2.2: the pin keeps its role, field and take position");
    expect(pin->file != qa::FileIdentity{}, "RF-2.11: the pin holds the file identity");

    const auto again = qa::pin_material(qa::MaterialPinRole::take, "--take", path, 2);
    expect(pinned(again) != nullptr && *pinned(again) == *pin,
           "RF-2.11: the same file gives the same pin");

    write_file(path, "other-content");
    const auto changed = qa::pin_material(qa::MaterialPinRole::take, "--take", path, 2);
    expect(pinned(changed) != nullptr && pinned(changed)->content != pin->content,
           "RF-2.11: other content gives another SHA-256");

    // Same bytes in a new file that replaces it: the content matches but the file does not. The
    // new file is created while the old one exists, as an editor saving over it does, so POSIX
    // cannot hand it the old inode.
    const auto replacement = path.string() + ".new";
    write_file(replacement, "rom-content");
    std::filesystem::rename(replacement, path);
    const auto replaced = qa::pin_material(qa::MaterialPinRole::take, "--take", path, 2);
    expect(pinned(replaced) != nullptr && pinned(replaced)->content == pin->content &&
               pinned(replaced)->file != pin->file,
           "RF-2.11: a replaced file keeps its content hash but not its identity");
}

void rejections_name_their_field(const std::filesystem::path &root) {
    expect(rejected(qa::pin_material(qa::MaterialPinRole::rom, "--rom", root), "--rom",
                    qa::MaterialPinError::is_directory),
           "RF-2.2: a directory is rejected in its field");
    expect(rejected(qa::pin_material(qa::MaterialPinRole::take, "--take", root / "missing.ayr"),
                    "--take", qa::MaterialPinError::not_found),
           "RF-2.2: a missing file is rejected in its field");
    expect(rejected(
               qa::pin_material(qa::MaterialPinRole::core, "--core", root / "missing" / "core.dll"),
               "--core", qa::MaterialPinError::not_found),
           "RF-2.2: a file in a missing directory is rejected in its field");
#ifdef _WIN32
    const std::filesystem::path device{"NUL"};
#else
    const std::filesystem::path device{"/dev/null"};
#endif
    expect(rejected(qa::pin_material(qa::MaterialPinRole::pack, "--pack", device), "--pack",
                    qa::MaterialPinError::not_regular),
           "RF-2.2: a device is not a regular file");
}

// A link names another file, so it is rejected before anything is read. File symbolic
// links need a privilege on Windows; a junction is created without one and is also a
// link, so the case is always exercised.
void links_are_rejected(const std::filesystem::path &root) {
    const auto target_file = root / "target.ay";
    write_file(target_file, "pack");
    const auto file_link = root / "file-link.ay";
    std::error_code error;
    std::filesystem::create_symlink(target_file, file_link, error);
    if (!error)
        expect(rejected(qa::pin_material(qa::MaterialPinRole::pack, "--pack", file_link), "--pack",
                        qa::MaterialPinError::is_link),
               "RF-2.2: a symbolic link to a file is rejected");
    else
        std::cout << "file symbolic links unavailable (" << error.message()
                  << "); the junction covers links\n";

    const auto target_directory = root / "target-directory";
    std::filesystem::create_directory(target_directory);
    const auto directory_link = root / "directory-link";
#ifdef _WIN32
    std::string command = "mklink /J \"";
    command += directory_link.string();
    command += "\" \"";
    command += target_directory.string();
    command += "\" >NUL 2>&1";
    const bool created = std::system(command.c_str()) == 0;
#else
    std::filesystem::create_directory_symlink(target_directory, directory_link, error);
    const bool created = !error;
#endif
    expect(created, "the directory link for the test is created");
    if (created)
        expect(rejected(qa::pin_material(qa::MaterialPinRole::rom, "--rom", directory_link),
                        "--rom", qa::MaterialPinError::is_link),
               "RF-2.2: a link is rejected as a link, not followed");
    std::filesystem::remove(directory_link, error);
}

#ifdef _WIN32
// Another process writing the material: it cannot be pinned while a writer holds it.
void a_material_being_written_is_in_use(const std::filesystem::path &root) {
    const auto path = root / "busy.ayr";
    write_file(path, "take");
    std::ofstream writer{path, std::ios::binary | std::ios::app};
    expect(rejected(qa::pin_material(qa::MaterialPinRole::take, "--take", path), "--take",
                    qa::MaterialPinError::in_use),
           "RF-2.11: a material open for writing is reported in use");
}

qa::FieldIssue hold_issue(const qa::HoldResult &result) {
    const auto *issue = std::get_if<qa::FieldIssue>(&result);
    return issue != nullptr ? *issue : qa::FieldIssue{};
}

// BR-060 (RF-2.11): while a take runs its materials cannot be written, renamed or
// deleted, and a material that changed since the validation is never handed over.
void materials_are_held_during_a_take(const std::filesystem::path &root) {
    const auto path = root / "held.ayr";
    write_file(path, "take-content");
    const auto pinned_result = qa::pin_material(qa::MaterialPinRole::take, "--take[0]", path);
    const auto *pin = pinned(pinned_result);
    expect(pin != nullptr, "the material to hold is pinned");
    if (pin == nullptr)
        return;
    const std::vector<qa::MaterialPin> pins{*pin};
    {
        auto held = qa::hold_materials(pins);
        expect(std::holds_alternative<qa::HeldMaterials>(held) &&
                   std::get<qa::HeldMaterials>(held).size() == 1U,
               "RF-2.11: an unchanged material is held");
        std::ofstream writer{path, std::ios::binary | std::ios::app};
        expect(!writer.is_open(), "RF-2.11: a held material cannot be written");
        std::error_code error;
        std::filesystem::remove(path, error);
        expect(static_cast<bool>(error), "RF-2.11: a held material cannot be deleted");
        error.clear();
        std::filesystem::rename(path, root / "renamed.ayr", error);
        expect(static_cast<bool>(error), "RF-2.11: a held material cannot be renamed");
    }
    {
        std::ofstream writer{path, std::ios::binary | std::ios::app};
        expect(writer.is_open(), "RF-2.11: the material is released after the take");
        writer << "changed";
    }
    expect(hold_issue(qa::hold_materials(pins)) == qa::FieldIssue{"--take[0]", "material_changed"},
           "RF-2.11: a material changed after the validation is never handed over");
    write_file(path, "take-content");
    {
        std::ofstream writer{path, std::ios::binary | std::ios::app};
        const auto busy = qa::hold_materials(pins);
        expect(hold_issue(busy) == qa::FieldIssue{"--take[0]", "material_in_use"},
               "RF-2.11: a material open for writing is not handed over");
    }
    std::filesystem::remove(path);
    expect(hold_issue(qa::hold_materials(pins)) ==
               qa::FieldIssue{"--take[0]", "material_unavailable"},
           "RF-2.11: a material that disappeared stops the take");
}
#endif

void codes_are_stable() {
    expect(qa::material_pin_error_code(qa::MaterialPinError::not_found) == "material_not_found" &&
               qa::material_pin_error_code(qa::MaterialPinError::is_directory) ==
                   "material_is_directory" &&
               qa::material_pin_error_code(qa::MaterialPinError::is_link) == "material_is_link" &&
               qa::material_pin_error_code(qa::MaterialPinError::not_regular) ==
                   "material_not_regular" &&
               qa::material_pin_error_code(qa::MaterialPinError::in_use) == "material_in_use" &&
               qa::material_pin_error_code(qa::MaterialPinError::unreadable) ==
                   "material_unreadable",
           "RF-2.2: each rejection has its own code");
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "ayther-qa-material-preflight";
    std::error_code ignored;
    std::filesystem::remove(root / "directory-link", ignored);
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    regular_file_is_pinned(root);
    rejections_name_their_field(root);
    links_are_rejected(root);
#ifdef _WIN32
    a_material_being_written_is_in_use(root);
    materials_are_held_during_a_take(root);
#endif
    codes_are_stable();

    std::filesystem::remove_all(root, ignored);
    if (failures != 0)
        return 1;
    std::cout << "materials are pinned by content and file identity\n";
    return 0;
}
