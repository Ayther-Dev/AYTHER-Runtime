#pragma once

#include "audio_chunk.h"
#include "fact_model.h"
#include "integrated_evidence.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

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
    [[nodiscard]] std::optional<IntegratedEvidenceError>
    append_pcm(const AudioChunk &chunk) noexcept;
    [[nodiscard]] IntegratedEvidenceResult finish(const ReplayTraceSummary &transport_trace) noexcept;
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
