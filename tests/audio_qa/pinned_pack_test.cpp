#include "content_hash.h"
#include "pinned_pack.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

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
    require(static_cast<bool>(input), "staged_material_open_failed");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

const qa::PinnedPack &pack(const qa::PinnedPackResult &result, const char *const message) {
    const auto *value = std::get_if<qa::PinnedPack>(&result);
    require(value != nullptr, message);
    return *value;
}

void remove_tree(const std::filesystem::path &path) {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

} // namespace

int main() {
    const auto current = std::filesystem::current_path();
    const auto source = current / "qa-131-source";
    const auto staging = current / "qa-131-staged";
    const auto rejected = current / "qa-131-rejected";
    remove_tree(source);
    remove_tree(staging);
    remove_tree(rejected);
    try {
        const std::string hash_fixture(200, 'x');
        qa::ContentHasher incremental_hash;
        incremental_hash.update(bytes(std::string_view{hash_fixture}.substr(0, 13)));
        incremental_hash.update(bytes(std::string_view{hash_fixture}.substr(13, 51)));
        incremental_hash.update(bytes(std::string_view{hash_fixture}.substr(64)));
        require(incremental_hash.finish() == qa::identify_content(bytes(hash_fixture)),
                "streaming_hash_did_not_match_single_pass_hash");

        constexpr std::string_view pack_v1 = "AYPK-pack-v1";
        constexpr std::string_view pack_v2 = "AYPK-pack-v2";
        constexpr std::string_view sample_v1 = "RIFF-sample-v1";
        constexpr std::string_view sample_v2 = "RIFF-sample-v2";
        const auto pack_source = source / "game.ay";
        const auto sample_source = source / "tone.wav";
        write(pack_source, pack_v1);
        write(sample_source, sample_v1);

        const auto expected_pack = qa::identify_content(bytes(pack_v1));
        const auto expected_sample = qa::identify_content(bytes(sample_v1));
        std::vector<qa::ExternalPackResource> resources{
            {"samples/tone.wav", sample_source, expected_sample}};

        {
            const auto staged =
                qa::stage_pinned_pack(pack_source, expected_pack, resources, staging);
            const auto &pinned = pack(staged, "pack_materials_were_not_staged");
            const auto *sample = pinned.resolve_external("samples/tone.wav");
            require(sample != nullptr && pinned.pack().identity == expected_pack &&
                        sample->identity == expected_sample &&
                        pinned.external_resources().size() == 1 &&
                        pinned.resolve_external("samples/unregistered.wav") == nullptr,
                    "registered_material_set_was_not_preserved");

            write(pack_source, pack_v2);
            write(sample_source, sample_v2);
            require(read(pinned.pack().path) == pack_v1 && read(sample->path) == sample_v1,
                    "external_change_reached_private_materials");
        }
        require(!std::filesystem::exists(staging), "private_materials_survived_owner");

        const auto mismatch =
            qa::stage_pinned_pack(pack_source, expected_pack, resources, rejected);
        require(std::get_if<qa::PinnedPackError>(&mismatch) != nullptr &&
                    std::get<qa::PinnedPackError>(mismatch) ==
                        qa::PinnedPackError::identity_mismatch &&
                    !std::filesystem::exists(rejected),
                "changed_source_was_not_rejected_and_cleaned");

        const std::vector<qa::ExternalPackResource> traversal{
            {"../escape.wav", sample_source, qa::identify_content(bytes(sample_v2))}};
        const auto invalid = qa::stage_pinned_pack(
            pack_source, qa::identify_content(bytes(pack_v2)), traversal, rejected);
        require(std::get_if<qa::PinnedPackError>(&invalid) != nullptr &&
                    std::get<qa::PinnedPackError>(invalid) ==
                        qa::PinnedPackError::invalid_logical_path &&
                    !std::filesystem::exists(rejected),
                "external_resource_traversal_was_accepted");

        write(rejected / "sentinel", "preserve-me");
        const auto preexisting =
            qa::stage_pinned_pack(pack_source, qa::identify_content(bytes(pack_v2)), {}, rejected);
        require(std::get_if<qa::PinnedPackError>(&preexisting) != nullptr &&
                    std::get<qa::PinnedPackError>(preexisting) ==
                        qa::PinnedPackError::staging_path_exists &&
                    read(rejected / "sentinel") == "preserve-me",
                "preexisting_staging_root_was_not_preserved");

        remove_tree(source);
        remove_tree(rejected);
        std::puts("pinned_pack_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(source);
        remove_tree(staging);
        remove_tree(rejected);
        std::fprintf(stderr, "pinned_pack_test: %s\n", error.what());
        return 1;
    }
}
