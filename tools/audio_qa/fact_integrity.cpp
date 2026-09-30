#include "fact_integrity.h"

#include "checkpoint_store.h"

#include <limits>
#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ayther::audio_qa {
namespace {

struct FactIdHash {
    std::size_t operator()(const FactId &id) const noexcept {
        auto result = std::hash<std::string>{}(id.run_id);
        result ^= std::hash<std::string>{}(id.producer_id) + 0x9e3779b9U + (result << 6U) +
                  (result >> 2U);
        result ^= std::hash<std::uint64_t>{}(id.producer_sequence) + 0x9e3779b9U +
                  (result << 6U) + (result >> 2U);
        return result;
    }
};

struct StringPairHash {
    std::size_t operator()(const std::pair<std::string, std::string> &value) const noexcept {
        auto result = std::hash<std::string>{}(value.first);
        result ^= std::hash<std::string>{}(value.second) + 0x9e3779b9U + (result << 6U) +
                  (result >> 2U);
        return result;
    }
};

using ProducerKey = std::pair<std::string, std::string>;
using SharedStateKey = std::pair<std::string, std::string>;

ProducerKey producer(const FactId &id) { return {id.run_id, id.producer_id}; }

void fail(FactIntegrityAudit &audit, FactIntegrityIssue issue) {
    audit.evidence_result = EvidenceResult::incomplete;
    if (!audit.first_issue) {
        audit.first_issue = std::move(issue);
    }
}

} // namespace

FactIntegrityAudit
audit_fact_integrity(const std::span<const std::filesystem::path> fragment_paths,
                     std::vector<Fact> *const reopened_facts) noexcept {
    FactIntegrityAudit audit;
    try {
        if (reopened_facts != nullptr)
            reopened_facts->clear();
        if (fragment_paths.empty()) {
            fail(audit, {FactIntegrityIssueKind::empty_segment});
            return audit;
        }
        if (fragment_paths.size() > max_checkpoint_artifacts) {
            fail(audit, {FactIntegrityIssueKind::too_many_fragments});
            return audit;
        }

        std::vector<Fact> facts;
        std::unordered_map<FactId, std::size_t, FactIdHash> fact_indices;
        std::unordered_map<ProducerKey, std::uint64_t, StringPairHash> producer_sequences;
        const auto reserve_hint = fragment_paths.size() * 256U;
        facts.reserve(reserve_hint);
        fact_indices.reserve(reserve_hint);
        std::optional<std::uint64_t> fragment_sequence;
        for (const auto &path : fragment_paths) {
            auto reopened = read_fact_fragment(path);
            auto *fragment = std::get_if<StoredFactFragment>(&reopened);
            if (fragment == nullptr) {
                fail(audit, {FactIntegrityIssueKind::fragment_unreadable, path, std::nullopt,
                             std::nullopt, 0, 0, std::get<FactFragmentStoreError>(reopened)});
                return audit;
            }
            if (fragment_sequence) {
                const auto expected =
                    *fragment_sequence == (std::numeric_limits<std::uint64_t>::max)()
                        ? 0
                        : *fragment_sequence + 1;
                if (expected == 0 || fragment->sequence != expected) {
                    fail(audit, {FactIntegrityIssueKind::fragment_sequence_discontinuity, path,
                                 std::nullopt, std::nullopt, expected, fragment->sequence});
                    return audit;
                }
            }
            fragment_sequence = fragment->sequence;

            for (auto &fact : fragment->facts) {
                const auto existing = fact_indices.find(fact.id);
                if (existing != fact_indices.end()) {
                    if (facts[existing->second] == fact) {
                        ++audit.identical_duplicates;
                        continue;
                    }
                    fail(audit,
                         {FactIntegrityIssueKind::conflicting_fact_id, path, fact.id, std::nullopt,
                          fact.id.producer_sequence, fact.id.producer_sequence});
                    return audit;
                }

                const auto producer_key = producer(fact.id);
                const auto previous = producer_sequences.find(producer_key);
                if (previous != producer_sequences.end()) {
                    const auto expected =
                        previous->second == (std::numeric_limits<std::uint64_t>::max)()
                            ? 0
                            : previous->second + 1;
                    if (expected == 0 || fact.id.producer_sequence != expected) {
                        fail(audit, {FactIntegrityIssueKind::producer_sequence_discontinuity, path,
                                     fact.id, std::nullopt, expected, fact.id.producer_sequence});
                    }
                }
                producer_sequences[producer_key] = fact.id.producer_sequence;
                fact_indices.emplace(fact.id, facts.size());
                facts.push_back(std::move(fact));
                ++audit.unique_facts;
            }
            ++audit.verified_fragments;
        }

        for (const auto &fact : facts) {
            for (const auto &cause : fact.cause_ids) {
                const auto *internal = std::get_if<FactId>(&cause);
                if (internal == nullptr) {
                    continue;
                }
                if (fact_indices.contains(*internal)) {
                    ++audit.resolved_internal_causes;
                    continue;
                }
                ++audit.unresolved_internal_causes;
                fail(audit,
                     {FactIntegrityIssueKind::unresolved_internal_cause, {}, fact.id, *internal});
            }
        }
        if (audit.unresolved_internal_causes != 0) {
            if (reopened_facts != nullptr)
                *reopened_facts = std::move(facts);
            return audit;
        }

        std::unordered_map<SharedStateKey, std::vector<std::uint64_t>, StringPairHash>
            shared_state_orders;
        for (const auto &fact : facts) {
            if (fact.shared_state_order.availability == Availability::not_applicable) {
                ++audit.independent_facts;
                continue;
            }
            if (!fact.shared_state_order.value) {
                continue;
            }
            for (const auto &order : *fact.shared_state_order.value) {
                shared_state_orders[{fact.id.run_id, order.state_id}].push_back(order.sequence);
                ++audit.validated_shared_state_orders;
            }
        }
        for (auto &[state_key, sequences] : shared_state_orders) {
            std::sort(sequences.begin(), sequences.end());
            sequences.erase(std::unique(sequences.begin(), sequences.end()), sequences.end());
            std::optional<std::uint64_t> previous;
            for (const auto sequence : sequences) {
                if (previous) {
                    const auto expected = *previous == (std::numeric_limits<std::uint64_t>::max)()
                                              ? 0
                                              : *previous + 1;
                    if (expected == 0 || sequence != expected) {
                        fail(audit, {FactIntegrityIssueKind::shared_state_order_discontinuity,
                                     {},
                                     std::nullopt,
                                     std::nullopt,
                                     expected,
                                     sequence,
                                     std::nullopt,
                                     state_key.second});
                        if (reopened_facts != nullptr)
                            *reopened_facts = std::move(facts);
                        return audit;
                    }
                }
                previous = sequence;
            }
        }
        if (!audit.first_issue)
            audit.evidence_result = EvidenceResult::complete;
        if (reopened_facts != nullptr)
            *reopened_facts = std::move(facts);
        return audit;
    } catch (...) {
        fail(audit, {FactIntegrityIssueKind::fragment_unreadable});
        return audit;
    }
}

} // namespace ayther::audio_qa
