#pragma once

#include "material_staging_budget.h"
#include "reference_model.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::size_t max_external_pack_resources = 4096;

struct ExternalPackResource {
    std::string logical_path;
    std::filesystem::path source_path;
    ContentIdentity expected_identity;
    bool operator==(const ExternalPackResource &) const = default;
};

struct PinnedDiskMaterial {
    std::filesystem::path path;
    ContentIdentity identity;
    bool operator==(const PinnedDiskMaterial &) const = default;
};

struct PinnedExternalPackResource {
    std::string logical_path;
    PinnedDiskMaterial material;
    bool operator==(const PinnedExternalPackResource &) const = default;
};

class PinnedPack final {
  public:
    PinnedPack(std::filesystem::path root, PinnedDiskMaterial pack,
               std::vector<PinnedExternalPackResource> external_resources);
    ~PinnedPack();
    PinnedPack(const PinnedPack &) = delete;
    PinnedPack &operator=(const PinnedPack &) = delete;
    PinnedPack(PinnedPack &&other) noexcept;
    PinnedPack &operator=(PinnedPack &&other) noexcept;

    [[nodiscard]] const std::filesystem::path &root() const noexcept;
    [[nodiscard]] const PinnedDiskMaterial &pack() const noexcept;
    [[nodiscard]] const std::vector<PinnedExternalPackResource> &
    external_resources() const noexcept;
    [[nodiscard]] const PinnedDiskMaterial *
    resolve_external(std::string_view logical_path) const noexcept;

  private:
    void remove() noexcept;

    std::filesystem::path root_;
    PinnedDiskMaterial pack_;
    std::vector<PinnedExternalPackResource> external_resources_;
};

enum class PinnedPackError {
    invalid_path,
    invalid_logical_path,
    duplicate_logical_path,
    too_many_resources,
    aggregate_too_large,
    staging_path_exists,
    io_error,
    identity_mismatch,
};

using PinnedPackResult = std::variant<PinnedPack, PinnedPackError>;

[[nodiscard]] PinnedPackResult
stage_pinned_pack(const std::filesystem::path &pack_source,
                  const ContentIdentity &expected_pack_identity,
                  const std::vector<ExternalPackResource> &external_resources,
                  const std::filesystem::path &staging_root) noexcept;

} // namespace ayther::audio_qa
