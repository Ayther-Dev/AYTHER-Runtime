#pragma once

#include "trace_query.h"

#include <filesystem>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

struct EvidenceQueryEntry {
    FactId origin;
    EventAudioRoute route;
    bool operator==(const EvidenceQueryEntry &) const = default;
};

struct EvidenceQueryIndex {
    std::vector<EvidenceQueryEntry> entries;
    bool operator==(const EvidenceQueryIndex &) const = default;
};

enum class EvidenceQueryIndexError {
    invalid_content,
    too_large,
    io_error,
    malformed_document,
    incompatible_version,
};

using EvidenceQueryIndexResult = std::variant<EvidenceQueryIndex, EvidenceQueryIndexError>;
using EvidenceQueryIndexEncodeResult = std::variant<std::string, EvidenceQueryIndexError>;

[[nodiscard]] EvidenceQueryIndexResult
make_evidence_query_index(const FactId &origin, const EventAudioQueryResult &result);
[[nodiscard]] EvidenceQueryIndexEncodeResult
evidence_query_index_to_toml(const EvidenceQueryIndex &index);
[[nodiscard]] EvidenceQueryIndexResult evidence_query_index_from_toml(std::string_view text);
[[nodiscard]] EvidenceQueryIndexResult
read_evidence_query_index(const std::filesystem::path &path) noexcept;
[[nodiscard]] std::vector<EventAudioRoute> query_evidence_index(const EvidenceQueryIndex &index,
                                                                const FactId &origin);
[[nodiscard]] std::string format_evidence_query(const FactId &origin,
                                                std::span<const EventAudioRoute> routes);

} // namespace ayther::audio_qa
