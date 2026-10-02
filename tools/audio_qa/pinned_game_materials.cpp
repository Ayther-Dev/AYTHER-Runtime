#include "pinned_game_materials.h"

#include "content_hash.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace ayther::audio_qa {
namespace {

using CopyResult = std::variant<PinnedDiskMaterial, PinnedGameMaterialsError>;

std::optional<std::uint64_t> source_size(const std::filesystem::path &path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return std::nullopt;
    }
    return size;
}

CopyResult copy_verified(const std::filesystem::path &source,
                         const std::filesystem::path &destination,
                         const ContentIdentity &expected) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return PinnedGameMaterialsError::io_error;
    }
    auto partial = destination;
    partial += ".partial";
    std::ofstream output(partial, std::ios::binary | std::ios::trunc);
    if (!output) {
        return PinnedGameMaterialsError::io_error;
    }

    ContentHasher hasher;
    std::uint64_t copied_size{};
    std::array<std::byte, 64U * 1024U> buffer{};
    while (input) {
        input.read(reinterpret_cast<char *>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count <= 0) {
            break;
        }
        const auto count_size = static_cast<std::uint64_t>(count);
        if (copied_size > expected.byte_size || count_size > expected.byte_size - copied_size) {
            output.close();
            std::error_code ignored;
            std::filesystem::remove(partial, ignored);
            return PinnedGameMaterialsError::identity_mismatch;
        }
        copied_size += count_size;
        const auto chunk =
            std::span<const std::byte>{buffer}.first(static_cast<std::size_t>(count));
        hasher.update(chunk);
        output.write(reinterpret_cast<const char *>(chunk.data()), count);
        if (!output) {
            output.close();
            std::error_code ignored;
            std::filesystem::remove(partial, ignored);
            return PinnedGameMaterialsError::io_error;
        }
    }
    if (input.bad()) {
        output.close();
        std::error_code ignored;
        std::filesystem::remove(partial, ignored);
        return PinnedGameMaterialsError::io_error;
    }
    output.flush();
    if (!output) {
        output.close();
        std::error_code ignored;
        std::filesystem::remove(partial, ignored);
        return PinnedGameMaterialsError::io_error;
    }
    output.close();

    const auto identity = hasher.finish();
    if (identity != expected) {
        std::error_code ignored;
        std::filesystem::remove(partial, ignored);
        return PinnedGameMaterialsError::identity_mismatch;
    }
    std::error_code error;
    std::filesystem::rename(partial, destination, error);
    if (error) {
        std::filesystem::remove(partial, error);
        return PinnedGameMaterialsError::io_error;
    }
    return PinnedDiskMaterial{destination, identity};
}

std::filesystem::path private_name(const std::string &stem, const std::filesystem::path &source) {
    return stem + source.extension().string();
}

} // namespace

PinnedGameMaterials::PinnedGameMaterials(
    std::filesystem::path root, PinnedDiskMaterial rom, PinnedDiskMaterial core,
    std::array<PrivateGameMaterialReference, 2> evidence_references)
    : root_(std::move(root)), rom_(std::move(rom)), core_(std::move(core)),
      evidence_references_(std::move(evidence_references)) {}

PinnedGameMaterials::~PinnedGameMaterials() { remove(); }

PinnedGameMaterials::PinnedGameMaterials(PinnedGameMaterials &&other) noexcept
    : root_(std::move(other.root_)), rom_(std::move(other.rom_)), core_(std::move(other.core_)),
      evidence_references_(std::move(other.evidence_references_)) {
    other.root_.clear();
}

PinnedGameMaterials &PinnedGameMaterials::operator=(PinnedGameMaterials &&other) noexcept {
    if (this != &other) {
        remove();
        root_ = std::move(other.root_);
        rom_ = std::move(other.rom_);
        core_ = std::move(other.core_);
        evidence_references_ = std::move(other.evidence_references_);
        other.root_.clear();
    }
    return *this;
}

const std::filesystem::path &PinnedGameMaterials::root() const noexcept { return root_; }

const PinnedDiskMaterial &PinnedGameMaterials::rom() const noexcept { return rom_; }

const PinnedDiskMaterial &PinnedGameMaterials::core() const noexcept { return core_; }

const std::array<PrivateGameMaterialReference, 2> &
PinnedGameMaterials::evidence_references() const noexcept {
    return evidence_references_;
}

void PinnedGameMaterials::remove() noexcept {
    if (root_.empty()) {
        return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
    root_.clear();
}

PinnedGameMaterialsResult stage_pinned_game_materials(
    const std::filesystem::path &rom_source, const ContentIdentity &expected_rom_identity,
    const std::filesystem::path &core_source, const ContentIdentity &expected_core_identity,
    const std::filesystem::path &staging_root) noexcept {
    bool staging_created{};
    try {
        if (rom_source.empty() || core_source.empty() || staging_root.empty()) {
            return PinnedGameMaterialsError::invalid_path;
        }
        const auto rom_size = source_size(rom_source);
        const auto core_size = source_size(core_source);
        if (!rom_size || !core_size) {
            return PinnedGameMaterialsError::io_error;
        }
        if (*rom_size != expected_rom_identity.byte_size ||
            *core_size != expected_core_identity.byte_size) {
            return PinnedGameMaterialsError::identity_mismatch;
        }
        if (expected_rom_identity.byte_size > max_pinned_private_material_bytes ||
            expected_core_identity.byte_size >
                max_pinned_private_material_bytes - expected_rom_identity.byte_size) {
            return PinnedGameMaterialsError::aggregate_too_large;
        }

        std::error_code error;
        if (!std::filesystem::create_directory(staging_root, error)) {
            return error ? PinnedGameMaterialsError::io_error
                         : PinnedGameMaterialsError::staging_path_exists;
        }
        staging_created = true;
        const auto cleanup = [&staging_root]() {
            std::error_code ignored;
            std::filesystem::remove_all(staging_root, ignored);
        };

        const auto rom = copy_verified(rom_source, staging_root / private_name("game", rom_source),
                                       expected_rom_identity);
        const auto *pinned_rom = std::get_if<PinnedDiskMaterial>(&rom);
        if (pinned_rom == nullptr) {
            const auto failure = std::get<PinnedGameMaterialsError>(rom);
            cleanup();
            return failure;
        }
        const auto core = copy_verified(
            core_source, staging_root / private_name("core", core_source), expected_core_identity);
        const auto *pinned_core = std::get_if<PinnedDiskMaterial>(&core);
        if (pinned_core == nullptr) {
            const auto failure = std::get<PinnedGameMaterialsError>(core);
            cleanup();
            return failure;
        }

        const std::array references{
            PrivateGameMaterialReference{PrivateGameMaterialKind::rom, pinned_rom->identity},
            PrivateGameMaterialReference{PrivateGameMaterialKind::core, pinned_core->identity}};
        return PinnedGameMaterials{staging_root, *pinned_rom, *pinned_core, references};
    } catch (...) {
        if (staging_created) {
            std::error_code ignored;
            std::filesystem::remove_all(staging_root, ignored);
        }
        return PinnedGameMaterialsError::io_error;
    }
}

} // namespace ayther::audio_qa
