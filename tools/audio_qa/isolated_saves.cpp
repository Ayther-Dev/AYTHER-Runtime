#include "isolated_saves.h"

#include "content_hash.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <span>
#include <utility>

namespace ayther::audio_qa {
namespace {

struct FileIdentityResult {
    std::optional<ContentIdentity> identity;
    IsolatedSavesError error = IsolatedSavesError::io_error;
};

FileIdentityResult identify_file(const std::filesystem::path &path,
                                 const std::uint64_t expected_size) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    ContentHasher hasher;
    std::uint64_t copied{};
    std::array<std::byte, 64U * 1024U> buffer{};
    while (input) {
        input.read(reinterpret_cast<char *>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count <= 0) {
            break;
        }
        const auto count_size = static_cast<std::uint64_t>(count);
        if (copied > expected_size || count_size > expected_size - copied) {
            return {};
        }
        copied += count_size;
        hasher.update(std::span<const std::byte>{buffer}.first(static_cast<std::size_t>(count)));
    }
    if (input.bad() || copied != expected_size) {
        return {};
    }
    return {hasher.finish(), IsolatedSavesError::io_error};
}

std::optional<std::filesystem::path> absolute_normal(const std::filesystem::path &path) {
    std::error_code error;
    const auto exists = std::filesystem::exists(path, error);
    if (error)
        return std::nullopt;
    if (exists) {
        auto canonical = std::filesystem::weakly_canonical(path, error);
        return error ? std::nullopt
                     : std::optional<std::filesystem::path>{canonical.lexically_normal()};
    }
    auto parent = std::filesystem::weakly_canonical(path.parent_path(), error);
    if (error)
        return std::nullopt;
    return (parent / path.filename()).lexically_normal();
}

bool contains(const std::filesystem::path &parent, const std::filesystem::path &candidate) {
    auto parent_part = parent.begin();
    auto candidate_part = candidate.begin();
    for (; parent_part != parent.end() && candidate_part != candidate.end();
         ++parent_part, ++candidate_part) {
        if (*parent_part != *candidate_part) {
            return false;
        }
    }
    return parent_part == parent.end();
}

} // namespace

SaveTreeSnapshotResult snapshot_save_tree(const std::filesystem::path &root) noexcept {
    try {
        if (root.empty()) {
            return IsolatedSavesError::invalid_path;
        }
        std::error_code error;
        const auto exists = std::filesystem::exists(root, error);
        if (error) {
            return IsolatedSavesError::io_error;
        }
        if (!exists) {
            return SaveTreeSnapshot{false, {}};
        }
        const auto root_status = std::filesystem::symlink_status(root, error);
        if (error) {
            return IsolatedSavesError::io_error;
        }
        if (std::filesystem::is_symlink(root_status)) {
            return IsolatedSavesError::unsupported_entry;
        }
        if (!std::filesystem::is_directory(root, error) || error) {
            return IsolatedSavesError::invalid_path;
        }

        SaveTreeSnapshot snapshot{true, {}};
        std::uint64_t aggregate{};
        std::filesystem::recursive_directory_iterator current(root, error);
        const std::filesystem::recursive_directory_iterator end;
        if (error) {
            return IsolatedSavesError::io_error;
        }
        while (current != end) {
            if (snapshot.entries.size() >= max_user_save_entries) {
                return IsolatedSavesError::too_many_entries;
            }
            const auto status = current->symlink_status(error);
            if (error) {
                return IsolatedSavesError::io_error;
            }
            const auto relative = std::filesystem::relative(current->path(), root, error);
            if (error || relative.empty() || relative.has_root_path()) {
                return IsolatedSavesError::io_error;
            }
            if (std::filesystem::is_symlink(status)) {
                return IsolatedSavesError::unsupported_entry;
            }
            if (std::filesystem::is_directory(status)) {
                snapshot.entries.push_back({relative.generic_string(), true, {}});
            } else if (std::filesystem::is_regular_file(status)) {
                const auto size = current->file_size(error);
                if (error) {
                    return IsolatedSavesError::io_error;
                }
                if (size > max_user_save_bytes - aggregate) {
                    return IsolatedSavesError::aggregate_too_large;
                }
                aggregate += size;
                const auto identity = identify_file(current->path(), size);
                if (!identity.identity) {
                    return identity.error;
                }
                snapshot.entries.push_back({relative.generic_string(), false, *identity.identity});
            } else {
                return IsolatedSavesError::unsupported_entry;
            }
            current.increment(error);
            if (error) {
                return IsolatedSavesError::io_error;
            }
        }
        std::ranges::sort(snapshot.entries, {}, &SaveTreeEntry::relative_path);
        return snapshot;
    } catch (...) {
        return IsolatedSavesError::io_error;
    }
}

IsolatedSaves::IsolatedSaves(std::filesystem::path private_directory,
                             std::filesystem::path user_saves_directory,
                             SaveTreeSnapshot user_snapshot)
    : private_directory_(std::move(private_directory)),
      user_saves_directory_(std::move(user_saves_directory)),
      user_snapshot_(std::move(user_snapshot)) {}

IsolatedSaves::~IsolatedSaves() { remove(); }

IsolatedSaves::IsolatedSaves(IsolatedSaves &&other) noexcept
    : private_directory_(std::move(other.private_directory_)),
      user_saves_directory_(std::move(other.user_saves_directory_)),
      user_snapshot_(std::move(other.user_snapshot_)) {
    other.private_directory_.clear();
}

IsolatedSaves &IsolatedSaves::operator=(IsolatedSaves &&other) noexcept {
    if (this != &other) {
        remove();
        private_directory_ = std::move(other.private_directory_);
        user_saves_directory_ = std::move(other.user_saves_directory_);
        user_snapshot_ = std::move(other.user_snapshot_);
        other.private_directory_.clear();
    }
    return *this;
}

const std::filesystem::path &IsolatedSaves::directory() const noexcept {
    return private_directory_;
}

const SaveTreeSnapshot &IsolatedSaves::user_snapshot() const noexcept { return user_snapshot_; }

SaveTreeVerificationResult IsolatedSaves::verify_user_saves_unchanged() const noexcept {
    const auto current = snapshot_save_tree(user_saves_directory_);
    if (const auto *error = std::get_if<IsolatedSavesError>(&current)) {
        return *error;
    }
    return std::get<SaveTreeSnapshot>(current) == user_snapshot_;
}

void IsolatedSaves::remove() noexcept {
    if (private_directory_.empty()) {
        return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(private_directory_, ignored);
    private_directory_.clear();
}

IsolatedSavesResult
prepare_isolated_saves(const std::filesystem::path &user_saves_directory,
                       const std::filesystem::path &private_directory) noexcept {
    bool private_created{};
    try {
        if (user_saves_directory.empty() || private_directory.empty()) {
            return IsolatedSavesError::invalid_path;
        }
        const auto user_absolute = absolute_normal(user_saves_directory);
        const auto private_absolute = absolute_normal(private_directory);
        if (!user_absolute || !private_absolute)
            return IsolatedSavesError::io_error;
        if (contains(*user_absolute, *private_absolute) ||
            contains(*private_absolute, *user_absolute)) {
            return IsolatedSavesError::overlapping_paths;
        }
        const auto snapshot = snapshot_save_tree(user_saves_directory);
        const auto *user_snapshot = std::get_if<SaveTreeSnapshot>(&snapshot);
        if (user_snapshot == nullptr) {
            return std::get<IsolatedSavesError>(snapshot);
        }

        std::error_code error;
        if (!std::filesystem::create_directory(private_directory, error)) {
            return error ? IsolatedSavesError::io_error : IsolatedSavesError::staging_path_exists;
        }
        private_created = true;
        return IsolatedSaves{private_directory, user_saves_directory, *user_snapshot};
    } catch (...) {
        if (private_created) {
            std::error_code ignored;
            std::filesystem::remove_all(private_directory, ignored);
        }
        return IsolatedSavesError::io_error;
    }
}

} // namespace ayther::audio_qa
