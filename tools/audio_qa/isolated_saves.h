#pragma once

#include "reference_model.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::size_t max_user_save_entries = 4096;
inline constexpr std::uint64_t max_user_save_bytes = 1ULL * 1024ULL * 1024ULL * 1024ULL;

struct SaveTreeEntry {
    std::string relative_path;
    bool directory{};
    ContentIdentity identity;
    bool operator==(const SaveTreeEntry &) const = default;
};

struct SaveTreeSnapshot {
    bool root_existed{};
    std::vector<SaveTreeEntry> entries;
    bool operator==(const SaveTreeSnapshot &) const = default;
};

enum class IsolatedSavesError {
    invalid_path,
    overlapping_paths,
    staging_path_exists,
    too_many_entries,
    aggregate_too_large,
    unsupported_entry,
    io_error,
};

using SaveTreeSnapshotResult = std::variant<SaveTreeSnapshot, IsolatedSavesError>;
using SaveTreeVerificationResult = std::variant<bool, IsolatedSavesError>;

[[nodiscard]] SaveTreeSnapshotResult snapshot_save_tree(const std::filesystem::path &root) noexcept;

class IsolatedSaves final {
  public:
    IsolatedSaves(std::filesystem::path private_directory,
                  std::filesystem::path user_saves_directory, SaveTreeSnapshot user_snapshot);
    ~IsolatedSaves();
    IsolatedSaves(const IsolatedSaves &) = delete;
    IsolatedSaves &operator=(const IsolatedSaves &) = delete;
    IsolatedSaves(IsolatedSaves &&other) noexcept;
    IsolatedSaves &operator=(IsolatedSaves &&other) noexcept;

    [[nodiscard]] const std::filesystem::path &directory() const noexcept;
    [[nodiscard]] const SaveTreeSnapshot &user_snapshot() const noexcept;
    [[nodiscard]] SaveTreeVerificationResult verify_user_saves_unchanged() const noexcept;

  private:
    void remove() noexcept;

    std::filesystem::path private_directory_;
    std::filesystem::path user_saves_directory_;
    SaveTreeSnapshot user_snapshot_;
};

using IsolatedSavesResult = std::variant<IsolatedSaves, IsolatedSavesError>;

[[nodiscard]] IsolatedSavesResult
prepare_isolated_saves(const std::filesystem::path &user_saves_directory,
                       const std::filesystem::path &private_directory) noexcept;

} // namespace ayther::audio_qa
