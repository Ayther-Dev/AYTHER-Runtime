#pragma once

#include "durable_file.h"
#include "exclusive_evidence_directory.h"
#include "reference_model.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::size_t max_checkpoint_artifacts = 65536;

enum class CheckpointArtifactKind { fact_fragment, pcm_block };

struct DurableArtifactPublication {
    CheckpointArtifactKind kind{CheckpointArtifactKind::fact_fragment};
    std::uint64_t sequence{};
    DurablePublishedFile publication;
};

struct CheckpointArtifact {
    CheckpointArtifactKind kind{CheckpointArtifactKind::fact_fragment};
    std::string relative_path;
    std::uint64_t sequence{};
    ContentIdentity identity;
    bool operator==(const CheckpointArtifact &) const = default;
};

struct EvidenceCheckpoint {
    std::string checkpoint_id;
    std::uint64_t sequence{};
    std::vector<CheckpointArtifact> artifacts;
    bool operator==(const EvidenceCheckpoint &) const = default;
};

class StoredCheckpoint final {
  public:
    [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }
    [[nodiscard]] const ContentIdentity &document_identity() const noexcept {
        return document_identity_;
    }
    [[nodiscard]] const EvidenceCheckpoint &checkpoint() const noexcept { return checkpoint_; }
    bool operator==(const StoredCheckpoint &) const = default;

  private:
    StoredCheckpoint(std::filesystem::path path, ContentIdentity document_identity,
                     EvidenceCheckpoint checkpoint)
        : path_(std::move(path)), document_identity_(document_identity),
          checkpoint_(std::move(checkpoint)) {}

    std::filesystem::path path_;
    ContentIdentity document_identity_;
    EvidenceCheckpoint checkpoint_;

    friend struct CheckpointStoreFactory;
};

enum class CheckpointStoreError {
    invalid_checkpoint,
    document_too_large,
    artifact_outside_run,
    artifact_invalid,
    artifact_changed,
    non_increasing_sequence,
    existing_checkpoint_invalid,
    publish_failed,
    io_error,
    malformed_document,
    incompatible_version,
};

struct UnverifiableCheckpointArtifact {
    CheckpointArtifact artifact;
    CheckpointStoreError reason{CheckpointStoreError::artifact_invalid};
    bool operator==(const UnverifiableCheckpointArtifact &) const = default;
};

struct CheckpointArtifactAudit {
    std::filesystem::path path;
    ContentIdentity document_identity;
    EvidenceCheckpoint checkpoint;
    std::vector<CheckpointArtifact> verified_artifacts;
    std::optional<UnverifiableCheckpointArtifact> first_unverifiable;
    bool operator==(const CheckpointArtifactAudit &) const = default;
};

using CheckpointStoreResult = std::variant<StoredCheckpoint, CheckpointStoreError>;
using CheckpointArtifactAuditResult = std::variant<CheckpointArtifactAudit, CheckpointStoreError>;

[[nodiscard]] CheckpointStoreResult
write_checkpoint(const ExclusiveEvidenceDirectory &directory, std::string checkpoint_id,
                 std::uint64_t sequence, const std::vector<DurableArtifactPublication> &artifacts,
                 DurablePublicationLimits limits = {}) noexcept;

[[nodiscard]] CheckpointStoreResult read_checkpoint(const std::filesystem::path &path) noexcept;

[[nodiscard]] CheckpointArtifactAuditResult
audit_checkpoint_artifacts(const std::filesystem::path &path) noexcept;

} // namespace ayther::audio_qa
