#pragma once

#include "reference_model.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <utility>
#include <variant>

namespace ayther::audio_qa {

class DurablePublishedFile final {
  public:
    [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }
    [[nodiscard]] const ContentIdentity &identity() const noexcept { return identity_; }
    bool operator==(const DurablePublishedFile &) const = default;

  private:
    DurablePublishedFile(std::filesystem::path path, ContentIdentity identity)
        : path_(std::move(path)), identity_(identity) {}

    std::filesystem::path path_;
    ContentIdentity identity_;

    friend struct DurableFileFactory;
};

enum class DurablePublishError {
    invalid_target,
    temporary_collision,
    write_failed,
    flush_failed,
    publish_failed,
    storage_exhausted,
    interrupted_before_flush,
    interrupted_after_flush,
};

enum class DurablePublicationStop { none, before_flush, after_flush };

struct DurablePublicationLimits {
    std::optional<std::size_t> writable_bytes;
    DurablePublicationStop stop{DurablePublicationStop::none};
};

using DurablePublishResult = std::variant<DurablePublishedFile, DurablePublishError>;

[[nodiscard]] DurablePublishResult
publish_durable_file(const std::filesystem::path &target, std::span<const std::byte> bytes,
                     DurablePublicationLimits limits = {}) noexcept;

} // namespace ayther::audio_qa
