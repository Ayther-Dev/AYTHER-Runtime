#pragma once

#include "audio_chunk.h"
#include "audio_integrity.h"
#include "fact_model.h"
#include "integrated_evidence.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

class IncrementalEvidenceWriter final {
  public:
    struct Impl;

    explicit IncrementalEvidenceWriter(std::unique_ptr<Impl> impl) noexcept;
    ~IncrementalEvidenceWriter();
    IncrementalEvidenceWriter(const IncrementalEvidenceWriter &) = delete;
    IncrementalEvidenceWriter &operator=(const IncrementalEvidenceWriter &) = delete;
    IncrementalEvidenceWriter(IncrementalEvidenceWriter &&) noexcept;
    IncrementalEvidenceWriter &operator=(IncrementalEvidenceWriter &&) noexcept;

    [[nodiscard]] std::optional<IntegratedEvidenceError>
    append_facts(std::span<const Fact> facts) noexcept;
    // Spec 002, DI-14: within a segment the PCM is contiguous; a chunk that starts a later
    // segment may start anywhere on the line. Whether the traversal needed one interval is
    // decided at `finish`, once the traversal is known.
    [[nodiscard]] std::optional<IntegratedEvidenceError>
    append_pcm(const AudioChunk &chunk) noexcept;
    // The intervals kept so far, one per segment.
    [[nodiscard]] std::vector<PcmSegmentInterval> pcm_segments() const;
    [[nodiscard]] IntegratedEvidenceResult
    finish(const ReplayTraceSummary &transport_trace, bool require_hd_relationships = true,
           PcmContinuity continuity = PcmContinuity::single) noexcept;
    [[nodiscard]] ReplayTraceSummary trace() const noexcept;

  private:
    std::unique_ptr<Impl> impl_;
};

using IncrementalEvidenceOpenResult =
    std::variant<IncrementalEvidenceWriter, IntegratedEvidenceError>;

[[nodiscard]] IncrementalEvidenceOpenResult
open_incremental_evidence(const std::filesystem::path &output_root,
                          std::string_view run_id) noexcept;

} // namespace ayther::audio_qa
