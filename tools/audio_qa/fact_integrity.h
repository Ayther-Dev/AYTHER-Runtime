#pragma once

#include "fact_fragment_store.h"
#include "model.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ayther::audio_qa {

enum class FactIntegrityIssueKind {
    empty_segment,
    too_many_fragments,
    fragment_unreadable,
    fragment_sequence_discontinuity,
    producer_sequence_discontinuity,
    conflicting_fact_id,
    unresolved_internal_cause,
    shared_state_order_discontinuity,
};

struct FactIntegrityIssue {
    FactIntegrityIssueKind kind{FactIntegrityIssueKind::empty_segment};
    std::filesystem::path fragment;
    std::optional<FactId> fact_id;
    std::optional<FactId> related_fact_id;
    std::uint64_t expected_sequence{};
    std::uint64_t observed_sequence{};
    std::optional<FactFragmentStoreError> store_error;
    std::string state_id;
    bool operator==(const FactIntegrityIssue &) const = default;
};

struct FactIntegrityAudit {
    EvidenceResult evidence_result{EvidenceResult::incomplete};
    std::size_t verified_fragments{};
    std::size_t unique_facts{};
    std::size_t identical_duplicates{};
    std::size_t resolved_internal_causes{};
    std::size_t unresolved_internal_causes{};
    std::size_t independent_facts{};
    std::size_t validated_shared_state_orders{};
    std::optional<FactIntegrityIssue> first_issue;
    bool operator==(const FactIntegrityAudit &) const = default;
};

[[nodiscard]] FactIntegrityAudit
audit_fact_integrity(std::span<const std::filesystem::path> fragment_paths,
                     std::vector<Fact> *reopened_facts = nullptr) noexcept;

} // namespace ayther::audio_qa
