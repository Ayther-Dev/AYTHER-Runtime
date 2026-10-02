#pragma once

#include "exclusive_evidence_directory.h"
#include "reference_model.h"

#include <filesystem>
#include <variant>

namespace ayther::audio_qa {

struct StoredReference {
    std::filesystem::path path;
    ContentIdentity document_identity;
    Reference reference;
    bool operator==(const StoredReference &) const = default;
};

enum class ReferenceStoreError {
    invalid_reference,
    document_too_large,
    already_exists,
    io_error,
    malformed_document,
    incompatible_version,
    invalid_document,
};

using ReferenceStoreResult = std::variant<StoredReference, ReferenceStoreError>;

[[nodiscard]] ReferenceStoreResult
write_immutable_reference(const ExclusiveEvidenceDirectory &directory,
                          const Reference &reference) noexcept;

[[nodiscard]] ReferenceStoreResult read_reference(const std::filesystem::path &path) noexcept;

} // namespace ayther::audio_qa
