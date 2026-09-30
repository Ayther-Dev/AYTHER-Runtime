#include "incremental_evidence.h"

#include "exclusive_evidence_directory.h"
#include "fact_fragment_store.h"
#include "fact_trace_summary.h"
#include "pcm_block_store.h"

#include <deque>
#include <future>
#include <limits>
#include <string>
#include <utility>

namespace ayther::audio_qa {
namespace {

bool same_stream(const AudioChunk &left, const AudioChunk &right) noexcept {
    return left.run_id == right.run_id && left.capture_point == right.capture_point &&
           left.format.pcm == right.format.pcm &&
           left.format.sample_rate == right.format.sample_rate &&
           left.format.channels == right.format.channels &&
           left.range.timeline_id == right.range.timeline_id &&
           left.range.sample_rate == right.range.sample_rate;
}

} // namespace

struct IncrementalEvidenceWriter::Impl {
    struct FactArtifact {
        std::filesystem::path path;
        std::uint64_t sequence{};
        std::uint32_t record_count{};
        ContentIdentity document_identity;
    };

    struct PcmArtifact {
        std::filesystem::path path;
        std::uint64_t sequence{};
        ContentIdentity document_identity;
    };

    struct PendingFactArtifact {
        std::uint64_t sequence{};
        std::uint32_t record_count{};
        std::future<FactFragmentStoreResult> publication;
    };

    Impl(ExclusiveEvidenceDirectory value, std::string_view run_id)
        : directory(std::move(value)), run_id(run_id), trace(std::string{run_id}) {}

    ExclusiveEvidenceDirectory directory;
    std::string run_id;
    ReplayFactTraceAccumulator trace;
    std::optional<AudioChunk> previous_pcm;
    std::vector<FactArtifact> fact_artifacts;
    std::deque<PendingFactArtifact> pending_fact_artifacts;
    std::vector<PcmArtifact> pcm_artifacts;
    ReplayTraceSummary final_trace;
    std::uint64_t fact_fragment_sequence{1U};
    std::uint64_t pcm_sequence{1U};
    std::uint64_t facts{};
    std::uint64_t pcm_blocks{};
    std::uint64_t pcm_bytes{};
    bool valid{true};
    bool finished{};

    [[nodiscard]] std::optional<IntegratedEvidenceError> resolve_oldest_fact() noexcept {
        if (pending_fact_artifacts.empty())
            return std::nullopt;
        try {
            auto pending = std::move(pending_fact_artifacts.front());
            pending_fact_artifacts.pop_front();
            const auto stored = pending.publication.get();
            const auto *fragment = std::get_if<StoredFactFragment>(&stored);
            if (fragment == nullptr || fragment->sequence != pending.sequence ||
                fragment->record_count != pending.record_count) {
                valid = false;
                return IntegratedEvidenceError::fact_publication_failed;
            }
            fact_artifacts.push_back({fragment->path, fragment->sequence, fragment->record_count,
                                      fragment->document_identity});
            return std::nullopt;
        } catch (...) {
            valid = false;
            return IntegratedEvidenceError::fact_publication_failed;
        }
    }
};

constexpr std::size_t max_pending_fact_publications = 16U;

IncrementalEvidenceWriter::IncrementalEvidenceWriter(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
IncrementalEvidenceWriter::~IncrementalEvidenceWriter() = default;
IncrementalEvidenceWriter::IncrementalEvidenceWriter(IncrementalEvidenceWriter &&) noexcept =
    default;
IncrementalEvidenceWriter &
IncrementalEvidenceWriter::operator=(IncrementalEvidenceWriter &&) noexcept = default;

std::optional<IntegratedEvidenceError>
IncrementalEvidenceWriter::append_facts(const std::span<const Fact> facts) noexcept {
    if (!impl_ || !impl_->valid || impl_->finished || facts.empty())
        return IntegratedEvidenceError::invalid_input;
    try {
        if (impl_->pending_fact_artifacts.size() >= max_pending_fact_publications) {
            if (const auto error = impl_->resolve_oldest_fact())
                return error;
        }
        std::vector<Fact> batch{facts.begin(), facts.end()};
        for (const auto &fact : batch) {
            if (!impl_->trace.consume(fact)) {
                impl_->valid = false;
                return IntegratedEvidenceError::fact_integrity_failed;
            }
        }
        if (facts.size() > (std::numeric_limits<std::uint64_t>::max)() - impl_->facts) {
            impl_->valid = false;
            return IntegratedEvidenceError::invalid_input;
        }
        impl_->facts += facts.size();
        const auto sequence = impl_->fact_fragment_sequence;
        const auto record_count = static_cast<std::uint32_t>(batch.size());
        if (impl_->fact_artifacts.empty() && impl_->pending_fact_artifacts.empty()) {
            const auto stored = write_fact_fragment(impl_->directory, sequence, batch);
            const auto *fragment = std::get_if<StoredFactFragment>(&stored);
            if (fragment == nullptr) {
                impl_->valid = false;
                return IntegratedEvidenceError::fact_publication_failed;
            }
            impl_->fact_artifacts.push_back({fragment->path, fragment->sequence,
                                             fragment->record_count, fragment->document_identity});
        } else {
            auto *const directory = &impl_->directory;
            auto publication =
                std::async(std::launch::async,
                           [directory, sequence, batch = std::move(batch)]() mutable noexcept {
                               return write_fact_fragment(*directory, sequence, batch);
                           });
            impl_->pending_fact_artifacts.push_back(
                {sequence, record_count, std::move(publication)});
        }
        ++impl_->fact_fragment_sequence;
        return std::nullopt;
    } catch (...) {
        impl_->valid = false;
        return IntegratedEvidenceError::fact_publication_failed;
    }
}

std::optional<IntegratedEvidenceError>
IncrementalEvidenceWriter::append_pcm(const AudioChunk &chunk) noexcept {
    if (!impl_ || !impl_->valid || impl_->finished)
        return IntegratedEvidenceError::invalid_input;
    try {
        auto confirmed = chunk;
        confirmed.durability = Durability::confirmed;
        confirmed.checkpoint_id = {
            Availability::known, "audio-qa-pcm-" + std::to_string(impl_->pcm_sequence), {}};
        const bool continuity_failed =
            impl_->previous_pcm &&
            (!same_stream(*impl_->previous_pcm, confirmed) ||
             impl_->previous_pcm->range.end != confirmed.range.begin ||
             impl_->previous_pcm->producer_sequence >= confirmed.producer_sequence);
        const auto stored = write_pcm_block(impl_->directory, impl_->pcm_sequence, confirmed);
        const auto *block = std::get_if<StoredPcmBlock>(&stored);
        if (block == nullptr) {
            impl_->valid = false;
            return IntegratedEvidenceError::pcm_publication_failed;
        }
        if (confirmed.bytes.size() >
            (std::numeric_limits<std::uint64_t>::max)() - impl_->pcm_bytes) {
            impl_->valid = false;
            return IntegratedEvidenceError::invalid_input;
        }
        impl_->pcm_bytes += confirmed.bytes.size();
        ++impl_->pcm_blocks;
        impl_->pcm_artifacts.push_back({block->path, block->sequence, block->document_identity});
        ++impl_->pcm_sequence;
        impl_->previous_pcm = std::move(confirmed);
        if (continuity_failed) {
            impl_->valid = false;
            return IntegratedEvidenceError::pcm_continuity_failed;
        }
        return std::nullopt;
    } catch (...) {
        impl_->valid = false;
        return IntegratedEvidenceError::pcm_publication_failed;
    }
}

IntegratedEvidenceResult
IncrementalEvidenceWriter::finish(const ReplayTraceSummary &transport_trace) noexcept {
    if (!impl_ || !impl_->valid || impl_->finished || impl_->facts == 0U ||
        impl_->pcm_blocks == 0U || impl_->facts != transport_trace.observed_fact_count)
        return IntegratedEvidenceError::invalid_input;
    while (!impl_->pending_fact_artifacts.empty()) {
        if (const auto error = impl_->resolve_oldest_fact())
            return *error;
    }
    impl_->finished = true;
    ReplayFactTraceAccumulator reopened_trace{impl_->run_id};
    std::uint64_t reopened_facts{};
    for (const auto &artifact : impl_->fact_artifacts) {
        const auto reopened = read_fact_fragment(artifact.path);
        const auto *verified = std::get_if<StoredFactFragment>(&reopened);
        if (verified == nullptr || verified->sequence != artifact.sequence ||
            verified->record_count != artifact.record_count ||
            verified->document_identity != artifact.document_identity)
            return IntegratedEvidenceError::fact_reopen_failed;
        for (const auto &fact : verified->facts)
            if (!reopened_trace.consume(fact))
                return IntegratedEvidenceError::fact_integrity_failed;
        reopened_facts += verified->facts.size();
    }
    if (reopened_facts != impl_->facts)
        return IntegratedEvidenceError::fact_reopen_failed;

    std::optional<AudioChunk> previous_pcm;
    std::uint64_t reopened_pcm_bytes{};
    for (const auto &artifact : impl_->pcm_artifacts) {
        const auto reopened = read_pcm_block(artifact.path);
        const auto *verified = std::get_if<StoredPcmBlock>(&reopened);
        if (verified == nullptr || verified->sequence != artifact.sequence ||
            verified->document_identity != artifact.document_identity ||
            verified->chunk.durability != Durability::confirmed ||
            (previous_pcm &&
             (!same_stream(*previous_pcm, verified->chunk) ||
              previous_pcm->range.end != verified->chunk.range.begin ||
              previous_pcm->producer_sequence >= verified->chunk.producer_sequence)))
            return IntegratedEvidenceError::pcm_reopen_failed;
        reopened_pcm_bytes += verified->chunk.bytes.size();
        previous_pcm = verified->chunk;
    }
    if (impl_->pcm_artifacts.size() != impl_->pcm_blocks || reopened_pcm_bytes != impl_->pcm_bytes)
        return IntegratedEvidenceError::pcm_reopen_failed;

    impl_->final_trace = reopened_trace.summarize(transport_trace.loss_free);
    if (transport_trace.loss_free && !impl_->final_trace.causally_connected)
        return IntegratedEvidenceError::relationship_reopen_failed;
    return IntegratedEvidenceSummary{impl_->directory.path(),
                                     impl_->facts,
                                     impl_->pcm_blocks,
                                     impl_->pcm_bytes,
                                     impl_->final_trace.loss_free,
                                     impl_->final_trace.causally_connected};
}

ReplayTraceSummary IncrementalEvidenceWriter::trace() const noexcept {
    return impl_ ? impl_->final_trace : ReplayTraceSummary{};
}

IncrementalEvidenceOpenResult open_incremental_evidence(const std::filesystem::path &output_root,
                                                        const std::string_view run_id) noexcept {
    try {
        auto reserved = create_exclusive_evidence_directory(output_root / "runs", run_id);
        auto *directory = std::get_if<ExclusiveEvidenceDirectory>(&reserved);
        if (directory == nullptr)
            return IntegratedEvidenceError::directory_unavailable;
        return IncrementalEvidenceWriter{
            std::make_unique<IncrementalEvidenceWriter::Impl>(std::move(*directory), run_id)};
    } catch (...) {
        return IntegratedEvidenceError::directory_unavailable;
    }
}

} // namespace ayther::audio_qa
