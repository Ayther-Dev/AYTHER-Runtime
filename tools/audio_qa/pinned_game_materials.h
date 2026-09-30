#pragma once

#include "pinned_pack.h"

#include <array>
#include <filesystem>
#include <variant>

namespace ayther::audio_qa {

enum class PrivateGameMaterialKind { rom, core };
enum class EvidenceContentPolicy { excluded };

struct PrivateGameMaterialReference {
    PrivateGameMaterialKind kind = PrivateGameMaterialKind::rom;
    ContentIdentity identity;
    EvidenceContentPolicy content_policy = EvidenceContentPolicy::excluded;
    bool operator==(const PrivateGameMaterialReference &) const = default;
};

class PinnedGameMaterials final {
  public:
    PinnedGameMaterials(std::filesystem::path root, PinnedDiskMaterial rom, PinnedDiskMaterial core,
                        std::array<PrivateGameMaterialReference, 2> evidence_references);
    ~PinnedGameMaterials();
    PinnedGameMaterials(const PinnedGameMaterials &) = delete;
    PinnedGameMaterials &operator=(const PinnedGameMaterials &) = delete;
    PinnedGameMaterials(PinnedGameMaterials &&other) noexcept;
    PinnedGameMaterials &operator=(PinnedGameMaterials &&other) noexcept;

    [[nodiscard]] const std::filesystem::path &root() const noexcept;
    [[nodiscard]] const PinnedDiskMaterial &rom() const noexcept;
    [[nodiscard]] const PinnedDiskMaterial &core() const noexcept;
    [[nodiscard]] const std::array<PrivateGameMaterialReference, 2> &
    evidence_references() const noexcept;

  private:
    void remove() noexcept;

    std::filesystem::path root_;
    PinnedDiskMaterial rom_;
    PinnedDiskMaterial core_;
    std::array<PrivateGameMaterialReference, 2> evidence_references_;
};

enum class PinnedGameMaterialsError {
    invalid_path,
    aggregate_too_large,
    staging_path_exists,
    io_error,
    identity_mismatch,
};

using PinnedGameMaterialsResult = std::variant<PinnedGameMaterials, PinnedGameMaterialsError>;

[[nodiscard]] PinnedGameMaterialsResult stage_pinned_game_materials(
    const std::filesystem::path &rom_source, const ContentIdentity &expected_rom_identity,
    const std::filesystem::path &core_source, const ContentIdentity &expected_core_identity,
    const std::filesystem::path &staging_root) noexcept;

} // namespace ayther::audio_qa
