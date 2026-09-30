#include "content_hash.h"
#include "pinned_game_materials.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace qa = ayther::audio_qa;
namespace {

template <typename Value>
concept HasPathMember = requires(Value value) { value.path; };

static_assert(!HasPathMember<qa::PrivateGameMaterialReference>);

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::span<const std::byte> bytes(const std::string_view text) {
    return {reinterpret_cast<const std::byte *>(text.data()), text.size()};
}

void write(const std::filesystem::path &path, const std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "fixture_open_failed");
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    require(static_cast<bool>(output), "fixture_write_failed");
}

std::string read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "private_material_open_failed");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void remove_tree(const std::filesystem::path &path) {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

const qa::PinnedGameMaterials &materials(const qa::PinnedGameMaterialsResult &result) {
    const auto *value = std::get_if<qa::PinnedGameMaterials>(&result);
    require(value != nullptr, "game_materials_were_not_staged");
    return *value;
}

} // namespace

int main() {
    const auto current = std::filesystem::current_path();
    const auto source = current / "qa-132-source";
    const auto staging = current / "qa-132-private";
    const auto rejected = current / "qa-132-rejected";
    remove_tree(source);
    remove_tree(staging);
    remove_tree(rejected);
    try {
        constexpr std::string_view rom_v1 = "ROM-v1-private";
        constexpr std::string_view rom_v2 = "ROM-v2-private";
        constexpr std::string_view core_v1 = "CORE-v1-private";
        constexpr std::string_view core_v2 = "CORE-v2-private";
        const auto rom_source = source / "game.md";
        const auto core_source = source / "emulator.dll";
        write(rom_source, rom_v1);
        write(core_source, core_v1);
        const auto expected_rom = qa::identify_content(bytes(rom_v1));
        const auto expected_core = qa::identify_content(bytes(core_v1));

        {
            const auto staged = qa::stage_pinned_game_materials(
                rom_source, expected_rom, core_source, expected_core, staging);
            const auto &pinned = materials(staged);
            const auto &references = pinned.evidence_references();
            require(pinned.rom().identity == expected_rom &&
                        pinned.core().identity == expected_core &&
                        pinned.rom().path.extension() == ".md" &&
                        pinned.core().path.extension() == ".dll",
                    "private_material_identity_or_extension_was_not_preserved");
            require(references[0] ==
                            qa::PrivateGameMaterialReference{qa::PrivateGameMaterialKind::rom,
                                                             expected_rom} &&
                        references[1] ==
                            qa::PrivateGameMaterialReference{qa::PrivateGameMaterialKind::core,
                                                             expected_core} &&
                        references[0].content_policy == qa::EvidenceContentPolicy::excluded &&
                        references[1].content_policy == qa::EvidenceContentPolicy::excluded,
                    "evidence_reference_did_not_exclude_private_content");

            write(rom_source, rom_v2);
            write(core_source, core_v2);
            require(read(pinned.rom().path) == rom_v1 && read(pinned.core().path) == core_v1,
                    "source_change_reached_pinned_game_materials");
        }
        require(!std::filesystem::exists(staging), "private_game_materials_survived_owner");

        const auto mismatch = qa::stage_pinned_game_materials(rom_source, expected_rom, core_source,
                                                              expected_core, rejected);
        require(std::get_if<qa::PinnedGameMaterialsError>(&mismatch) != nullptr &&
                    std::get<qa::PinnedGameMaterialsError>(mismatch) ==
                        qa::PinnedGameMaterialsError::identity_mismatch &&
                    !std::filesystem::exists(rejected),
                "changed_game_material_was_not_rejected");

        write(rejected / "sentinel", "preserve-me");
        const auto preexisting = qa::stage_pinned_game_materials(
            rom_source, qa::identify_content(bytes(rom_v2)), core_source,
            qa::identify_content(bytes(core_v2)), rejected);
        require(std::get_if<qa::PinnedGameMaterialsError>(&preexisting) != nullptr &&
                    std::get<qa::PinnedGameMaterialsError>(preexisting) ==
                        qa::PinnedGameMaterialsError::staging_path_exists &&
                    read(rejected / "sentinel") == "preserve-me",
                "preexisting_private_root_was_not_preserved");

        remove_tree(source);
        remove_tree(rejected);
        std::puts("pinned_game_materials_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(source);
        remove_tree(staging);
        remove_tree(rejected);
        std::fprintf(stderr, "pinned_game_materials_test: %s\n", error.what());
        return 1;
    }
}
