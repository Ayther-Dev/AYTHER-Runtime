#include "pinned_pack.h"

#include "content_hash.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <span>
#include <utility>

namespace ayther::audio_qa {
namespace {

using CopyResult = std::variant<PinnedDiskMaterial, PinnedPackError>;

bool valid_logical_path(const std::string &value) {
    if (value.empty() || value.size() > 4096) {
        return false;
    }
    const std::filesystem::path path{value};
    if (path.is_absolute() || path.has_root_path()) {
        return false;
    }
    for (const auto &part : path) {
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
    }
    return path.lexically_normal().generic_string() == path.generic_string();
}

CopyResult copy_verified(const std::filesystem::path &source,
                         const std::filesystem::path &destination,
                         const ContentIdentity &expected) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return PinnedPackError::io_error;
    }
    auto partial = destination;
    partial += ".partial";
    std::ofstream output(partial, std::ios::binary | std::ios::trunc);
    if (!output) {
        return PinnedPackError::io_error;
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
            return PinnedPackError::identity_mismatch;
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
            return PinnedPackError::io_error;
        }
    }
    if (input.bad()) {
        output.close();
        std::error_code ignored;
        std::filesystem::remove(partial, ignored);
        return PinnedPackError::io_error;
    }
    output.flush();
    if (!output) {
        output.close();
        std::error_code ignored;
        std::filesystem::remove(partial, ignored);
        return PinnedPackError::io_error;
    }
    output.close();

    const auto identity = hasher.finish();
    if (identity != expected) {
        std::error_code ignored;
        std::filesystem::remove(partial, ignored);
        return PinnedPackError::identity_mismatch;
    }
    std::error_code error;
    std::filesystem::rename(partial, destination, error);
    if (error) {
        std::filesystem::remove(partial, error);
        return PinnedPackError::io_error;
    }
    return PinnedDiskMaterial{destination, identity};
}

std::optional<std::uint64_t> source_size(const std::filesystem::path &path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return std::nullopt;
    }
    return size;
}

} // namespace

PinnedPack::PinnedPack(std::filesystem::path root, PinnedDiskMaterial pack,
                       std::vector<PinnedExternalPackResource> external_resources)
    : root_(std::move(root)), pack_(std::move(pack)),
      external_resources_(std::move(external_resources)) {}

PinnedPack::~PinnedPack() { remove(); }

PinnedPack::PinnedPack(PinnedPack &&other) noexcept
    : root_(std::move(other.root_)), pack_(std::move(other.pack_)),
      external_resources_(std::move(other.external_resources_)) {
    other.root_.clear();
}

PinnedPack &PinnedPack::operator=(PinnedPack &&other) noexcept {
    if (this != &other) {
        remove();
        root_ = std::move(other.root_);
        pack_ = std::move(other.pack_);
        external_resources_ = std::move(other.external_resources_);
        other.root_.clear();
    }
    return *this;
}

const std::filesystem::path &PinnedPack::root() const noexcept { return root_; }

const PinnedDiskMaterial &PinnedPack::pack() const noexcept { return pack_; }

const std::vector<PinnedExternalPackResource> &PinnedPack::external_resources() const noexcept {
    return external_resources_;
}

const PinnedDiskMaterial *
PinnedPack::resolve_external(const std::string_view logical_path) const noexcept {
    const auto found = std::find_if(
        external_resources_.begin(), external_resources_.end(),
        [logical_path](const auto &resource) { return resource.logical_path == logical_path; });
    return found == external_resources_.end() ? nullptr : &found->material;
}

void PinnedPack::remove() noexcept {
    if (root_.empty()) {
        return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
    root_.clear();
}

PinnedPackResult stage_pinned_pack(const std::filesystem::path &pack_source,
                                   const ContentIdentity &expected_pack_identity,
                                   const std::vector<ExternalPackResource> &external_resources,
                                   const std::filesystem::path &staging_root) noexcept {
    bool staging_created{};
    try {
        if (pack_source.empty() || staging_root.empty()) {
            return PinnedPackError::invalid_path;
        }
        if (external_resources.size() > max_external_pack_resources) {
            return PinnedPackError::too_many_resources;
        }

        std::set<std::string> logical_paths;
        std::uint64_t aggregate{};
        const auto add_size =
            [&aggregate](const std::filesystem::path &path,
                         const ContentIdentity &expected) -> std::optional<PinnedPackError> {
            const auto size = source_size(path);
            if (!size) {
                return PinnedPackError::io_error;
            }
            if (*size != expected.byte_size) {
                return PinnedPackError::identity_mismatch;
            }
            if (expected.byte_size > max_pinned_private_material_bytes - aggregate) {
                return PinnedPackError::aggregate_too_large;
            }
            aggregate += expected.byte_size;
            return std::nullopt;
        };
        if (const auto failure = add_size(pack_source, expected_pack_identity)) {
            return *failure;
        }
        for (const auto &resource : external_resources) {
            if (!valid_logical_path(resource.logical_path)) {
                return PinnedPackError::invalid_logical_path;
            }
            if (!logical_paths.insert(resource.logical_path).second) {
                return PinnedPackError::duplicate_logical_path;
            }
            if (resource.source_path.empty()) {
                return PinnedPackError::invalid_path;
            }
            if (const auto failure = add_size(resource.source_path, resource.expected_identity)) {
                return *failure;
            }
        }

        std::error_code error;
        if (!std::filesystem::create_directory(staging_root, error)) {
            return error ? PinnedPackError::io_error : PinnedPackError::staging_path_exists;
        }
        staging_created = true;
        const auto cleanup = [&staging_root]() {
            std::error_code ignored;
            std::filesystem::remove_all(staging_root, ignored);
        };

        const auto pack =
            copy_verified(pack_source, staging_root / "pack.ay", expected_pack_identity);
        const auto *pinned_pack = std::get_if<PinnedDiskMaterial>(&pack);
        if (pinned_pack == nullptr) {
            const auto failure = std::get<PinnedPackError>(pack);
            cleanup();
            return failure;
        }

        std::vector<PinnedExternalPackResource> pinned_resources;
        pinned_resources.reserve(external_resources.size());
        for (const auto &resource : external_resources) {
            const auto destination = staging_root / "resources" / resource.logical_path;
            std::filesystem::create_directories(destination.parent_path(), error);
            if (error) {
                cleanup();
                return PinnedPackError::io_error;
            }
            const auto copied =
                copy_verified(resource.source_path, destination, resource.expected_identity);
            const auto *material = std::get_if<PinnedDiskMaterial>(&copied);
            if (material == nullptr) {
                const auto failure = std::get<PinnedPackError>(copied);
                cleanup();
                return failure;
            }
            pinned_resources.push_back({resource.logical_path, *material});
        }
        return PinnedPack{staging_root, *pinned_pack, std::move(pinned_resources)};
    } catch (...) {
        if (staging_created) {
            std::error_code ignored;
            std::filesystem::remove_all(staging_root, ignored);
        }
        return PinnedPackError::io_error;
    }
}

} // namespace ayther::audio_qa
