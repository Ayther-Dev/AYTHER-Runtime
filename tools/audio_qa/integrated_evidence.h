#pragma once

#include "audio_chunk.h"
#include "fact_model.h"
#include "replay_execution_result.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <variant>

namespace ayther::audio_qa {

struct IntegratedEvidenceSummary {
    std::filesystem::path directory;
    std::uint64_t facts{};
    std::uint64_t pcm_blocks{};
    std::uint64_t pcm_bytes{};
    bool fact_integrity_complete{};
    bool relationships_reopened{};
};

enum class IntegratedEvidenceError {
    invalid_input,
    directory_unavailable,
    fact_publication_failed,
    pcm_publication_failed,
    fact_reopen_failed,
    fact_integrity_failed,
    fact_sequence_gap,
    fact_unresolved_cause,
    fact_shared_order_gap,
    fact_content_mismatch,
    pcm_reopen_failed,
    pcm_continuity_failed,
    relationship_reopen_failed,
    // Spec 002, DI-14: the PCM segments do not match the linear segments of the traversal.
    pcm_segment_mismatch,
};

using IntegratedEvidenceResult = std::variant<IntegratedEvidenceSummary, IntegratedEvidenceError>;

[[nodiscard]] IntegratedEvidenceResult
persist_and_reopen_evidence(const std::filesystem::path &output_root, std::string_view run_id,
                            std::span<const Fact> facts, std::span<const AudioChunk> audio_chunks,
                            const ReplayTraceSummary &trace) noexcept;
[[nodiscard]] std::string_view
integrated_evidence_error_code(IntegratedEvidenceError error) noexcept;

} // namespace ayther::audio_qa
