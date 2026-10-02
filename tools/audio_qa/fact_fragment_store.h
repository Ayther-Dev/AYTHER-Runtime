#pragma once

#include "exclusive_evidence_directory.h"
#include "fact_model.h"
#include "reference_model.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::size_t fact_fragment_header_bytes = 64;

struct StoredFactFragment {
    std::filesystem::path path;
    std::uint64_t sequence{};
    std::uint32_t record_count{};
    ContentIdentity payload_identity;
    ContentIdentity document_identity;
    std::vector<Fact> facts;
    bool operator==(const StoredFactFragment &) const = default;
};

enum class FactFragmentStoreError {
    invalid_sequence,
    invalid_facts,
    document_too_large,
    already_exists,
    io_error,
    malformed_header,
    incompatible_version,
    length_mismatch,
    sequence_mismatch,
    hash_mismatch,
    count_mismatch,
    invalid_content,
};

using FactFragmentStoreResult = std::variant<StoredFactFragment, FactFragmentStoreError>;

[[nodiscard]] FactFragmentStoreResult
write_fact_fragment(const ExclusiveEvidenceDirectory &directory, std::uint64_t sequence,
                    const std::vector<Fact> &facts) noexcept;

[[nodiscard]] FactFragmentStoreResult
read_fact_fragment(const std::filesystem::path &path) noexcept;

} // namespace ayther::audio_qa
