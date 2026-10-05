#include "integrated_evidence.h"

#include "exclusive_evidence_directory.h"
#include "fact_batch.h"
#include "fact_fragment_store.h"
#include "fact_integrity.h"
#include "pcm_block_store.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ayther::audio_qa {
namespace {

[[nodiscard]] FactId domain_id(const std::string_view run_id, const ReplayTraceFactId id) {
    return {std::string{run_id}, "engine-" + std::to_string(id.producer), id.sequence};
}

[[nodiscard]] const Fact *find_fact(const std::span<const Fact> facts, const FactId &id) noexcept {
    const auto found =
        std::find_if(facts.begin(), facts.end(), [&](const Fact &fact) { return fact.id == id; });
    return found == facts.end() ? nullptr : &*found;
}

[[nodiscard]] bool ancestor(const std::span<const Fact> facts, const FactId &origin,
                            const FactId &descendant) {
    std::vector<FactId> pending{descendant};
    std::vector<FactId> visited;
    while (!pending.empty()) {
        const auto current = std::move(pending.back());
        pending.pop_back();
        if (current == origin)
            return true;
        if (std::find(visited.begin(), visited.end(), current) != visited.end())
            continue;
        visited.push_back(current);
        const auto *fact = find_fact(facts, current);
        if (fact == nullptr)
            continue;
        for (const auto &cause : fact->cause_ids)
            if (const auto *id = std::get_if<FactId>(&cause))
                pending.push_back(*id);
    }
    return false;
}

[[nodiscard]] bool same_pcm(const AudioChunk &expected, const AudioChunk &actual) noexcept {
    return expected.run_id == actual.run_id && expected.capture_point == actual.capture_point &&
           expected.producer_sequence == actual.producer_sequence &&
           expected.format.pcm == actual.format.pcm &&
           expected.format.sample_rate == actual.format.sample_rate &&
           expected.format.channels == actual.format.channels && expected.range == actual.range &&
           expected.bytes == actual.bytes && expected.sha256 == actual.sha256 &&
           expected.cause_ids == actual.cause_ids && expected.segment == actual.segment;
}

[[nodiscard]] bool reopened_relationships(const std::span<const Fact> facts,
                                          const std::string_view run_id,
                                          const ReplayTraceSummary &trace) {
    const auto ingress = domain_id(run_id, trace.ingress);
    const auto candidate = domain_id(run_id, trace.candidate);
    const auto selection = domain_id(run_id, trace.selection);
    const auto request = domain_id(run_id, trace.playback_request);
    const auto decision = domain_id(run_id, trace.playback_decision);
    const auto effect = domain_id(run_id, trace.playback_effect);
    const auto mix = domain_id(run_id, trace.mix_span);
    const auto *request_fact = find_fact(facts, request);
    const auto occurrence = std::to_string(trace.occurrence);
    return request_fact != nullptr && request_fact->occurrence_id.value &&
           *request_fact->occurrence_id.value == occurrence &&
           ancestor(facts, ingress, candidate) && ancestor(facts, candidate, selection) &&
           ancestor(facts, selection, request) && ancestor(facts, request, decision) &&
           ancestor(facts, decision, effect) && ancestor(facts, request, mix);
}

} // namespace

IntegratedEvidenceResult persist_and_reopen_evidence(const std::filesystem::path &output_root,
                                                     const std::string_view run_id,
                                                     const std::span<const Fact> facts,
                                                     const std::span<const AudioChunk> audio_chunks,
                                                     const ReplayTraceSummary &trace) noexcept {
    try {
        if (facts.empty() || audio_chunks.empty() || facts.size() != trace.observed_fact_count)
            return IntegratedEvidenceError::invalid_input;
        auto reserved = create_exclusive_evidence_directory(output_root / "runs", run_id);
        const auto *directory = std::get_if<ExclusiveEvidenceDirectory>(&reserved);
        if (directory == nullptr)
            return IntegratedEvidenceError::directory_unavailable;

        const std::vector<Fact> owned_facts{facts.begin(), facts.end()};
        std::vector<std::filesystem::path> fact_paths;
        std::size_t fact_offset{};
        std::uint64_t fact_sequence{1U};
        while (fact_offset < owned_facts.size()) {
            std::size_t count = std::min(max_fact_batch_records, owned_facts.size() - fact_offset);
            FactFragmentStoreResult fragment{FactFragmentStoreError::invalid_facts};
            for (;;) {
                std::vector<Fact> batch{
                    owned_facts.begin() + static_cast<std::ptrdiff_t>(fact_offset),
                    owned_facts.begin() + static_cast<std::ptrdiff_t>(fact_offset + count)};
                fragment = write_fact_fragment(*directory, fact_sequence, batch);
                const auto *error = std::get_if<FactFragmentStoreError>(&fragment);
                if (error == nullptr || *error != FactFragmentStoreError::document_too_large ||
                    count == 1U)
                    break;
                count /= 2U;
            }
            const auto *stored_fact = std::get_if<StoredFactFragment>(&fragment);
            if (stored_fact == nullptr)
                return IntegratedEvidenceError::fact_publication_failed;
            fact_paths.push_back(stored_fact->path);
            fact_offset += count;
            if (fact_sequence == (std::numeric_limits<std::uint64_t>::max)())
                return IntegratedEvidenceError::invalid_input;
            ++fact_sequence;
        }

        std::vector<std::filesystem::path> pcm_paths;
        pcm_paths.reserve(audio_chunks.size());
        std::uint64_t sequence = 1U;
        for (const auto &pending : audio_chunks) {
            auto confirmed = pending;
            confirmed.durability = Durability::confirmed;
            confirmed.checkpoint_id = {
                Availability::known, "qa172-pcm-" + std::to_string(sequence), {}};
            const auto stored = write_pcm_block(*directory, sequence, confirmed);
            const auto *block = std::get_if<StoredPcmBlock>(&stored);
            if (block == nullptr)
                return IntegratedEvidenceError::pcm_publication_failed;
            pcm_paths.push_back(block->path);
            if (sequence == (std::numeric_limits<std::uint64_t>::max)())
                return IntegratedEvidenceError::invalid_input;
            ++sequence;
        }

        std::vector<Fact> reopened_facts;
        const auto audit = audit_fact_integrity(fact_paths, &reopened_facts);
        const bool tolerated_shared_order_gap =
            audit.evidence_result == EvidenceResult::incomplete && audit.first_issue &&
            audit.first_issue->kind == FactIntegrityIssueKind::shared_state_order_discontinuity;
        if (trace.causally_connected && audit.evidence_result != EvidenceResult::complete) {
            if (!tolerated_shared_order_gap && audit.first_issue) {
                switch (audit.first_issue->kind) {
                case FactIntegrityIssueKind::producer_sequence_discontinuity:
                    return IntegratedEvidenceError::fact_sequence_gap;
                case FactIntegrityIssueKind::unresolved_internal_cause:
                    return IntegratedEvidenceError::fact_unresolved_cause;
                case FactIntegrityIssueKind::shared_state_order_discontinuity:
                    return IntegratedEvidenceError::fact_shared_order_gap;
                default:
                    break;
                }
            }
            if (!tolerated_shared_order_gap)
                return IntegratedEvidenceError::fact_integrity_failed;
        }
        if (trace.causally_connected && audit.unique_facts != facts.size())
            return IntegratedEvidenceError::fact_integrity_failed;
        if (reopened_facts != owned_facts)
            return IntegratedEvidenceError::fact_content_mismatch;
        const bool relationships_reopened =
            trace.causally_connected && reopened_relationships(reopened_facts, run_id, trace);
        if (trace.causally_connected && !relationships_reopened)
            return IntegratedEvidenceError::relationship_reopen_failed;

        std::uint64_t pcm_bytes{};
        for (std::size_t index{}; index < pcm_paths.size(); ++index) {
            const auto reopened = read_pcm_block(pcm_paths[index]);
            const auto *block = std::get_if<StoredPcmBlock>(&reopened);
            if (block == nullptr || !same_pcm(audio_chunks[index], block->chunk) ||
                block->chunk.durability != Durability::confirmed)
                return IntegratedEvidenceError::pcm_reopen_failed;
            if (block->chunk.bytes.size() > (std::numeric_limits<std::uint64_t>::max)() - pcm_bytes)
                return IntegratedEvidenceError::invalid_input;
            pcm_bytes += block->chunk.bytes.size();
        }
        return IntegratedEvidenceSummary{directory->path(),
                                         facts.size(),
                                         audio_chunks.size(),
                                         pcm_bytes,
                                         audit.evidence_result == EvidenceResult::complete,
                                         relationships_reopened};
    } catch (...) {
        return IntegratedEvidenceError::pcm_reopen_failed;
    }
}

std::string_view integrated_evidence_error_code(const IntegratedEvidenceError error) noexcept {
    switch (error) {
    case IntegratedEvidenceError::invalid_input:
        return "invalid_input";
    case IntegratedEvidenceError::directory_unavailable:
        return "directory_unavailable";
    case IntegratedEvidenceError::fact_publication_failed:
        return "fact_publication_failed";
    case IntegratedEvidenceError::pcm_publication_failed:
        return "pcm_publication_failed";
    case IntegratedEvidenceError::fact_reopen_failed:
        return "fact_reopen_failed";
    case IntegratedEvidenceError::fact_integrity_failed:
        return "fact_integrity_failed";
    case IntegratedEvidenceError::fact_sequence_gap:
        return "fact_sequence_gap";
    case IntegratedEvidenceError::fact_unresolved_cause:
        return "fact_unresolved_cause";
    case IntegratedEvidenceError::fact_shared_order_gap:
        return "fact_shared_order_gap";
    case IntegratedEvidenceError::fact_content_mismatch:
        return "fact_content_mismatch";
    case IntegratedEvidenceError::pcm_reopen_failed:
        return "pcm_reopen_failed";
    case IntegratedEvidenceError::pcm_continuity_failed:
        return "pcm_continuity_failed";
    case IntegratedEvidenceError::pcm_segment_mismatch:
        return "pcm_segment_mismatch";
    case IntegratedEvidenceError::relationship_reopen_failed:
        return "relationship_reopen_failed";
    }
    return "integrated_evidence_failed";
}

} // namespace ayther::audio_qa
