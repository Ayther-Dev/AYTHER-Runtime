#include "audio_integrity.h"

#include "checkpoint_store.h"

#include <limits>
#include <string>
#include <utility>
#include <variant>

namespace ayther::audio_qa {
namespace {

bool same_format(const AudioFormat &left, const AudioFormat &right) noexcept {
    return left.pcm == right.pcm && left.sample_rate == right.sample_rate &&
           left.channels == right.channels;
}

void fail(AudioIntegrityAudit &audit, AudioIntegrityIssue issue) {
    audit.evidence_result = EvidenceResult::incomplete;
    if (!audit.first_issue) {
        audit.first_issue = std::move(issue);
    }
}

} // namespace

PcmJoin join_pcm(const AudioChunk &previous, const AudioChunk &next,
                 const PcmContinuity continuity) noexcept {
    // DI-14: within an inspection a segment never comes back, and a later one may start
    // anywhere on the line: its frames follow a navigation produced silently.
    if (continuity == PcmContinuity::per_segment) {
        if (next.segment < previous.segment)
            return PcmJoin::segment_out_of_order;
        if (next.segment > previous.segment)
            return PcmJoin::starts_segment;
    }
    if (next.range.begin > previous.range.end)
        return PcmJoin::sample_gap;
    if (next.range.begin < previous.range.end)
        return PcmJoin::sample_overlap_or_reorder;
    return next.segment == previous.segment ? PcmJoin::continues : PcmJoin::starts_segment;
}

AudioIntegrityAudit audit_audio_integrity(const std::span<const std::filesystem::path> block_paths,
                                          const PcmContinuity continuity) noexcept {
    AudioIntegrityAudit audit;
    try {
        if (block_paths.empty()) {
            fail(audit, {AudioIntegrityIssueKind::empty_capture});
            return audit;
        }
        if (block_paths.size() > max_checkpoint_artifacts) {
            fail(audit, {AudioIntegrityIssueKind::too_many_blocks});
            return audit;
        }

        std::optional<std::uint64_t> block_sequence;
        std::optional<AudioChunk> previous;
        for (const auto &path : block_paths) {
            const auto reopened = read_pcm_block(path);
            const auto *block = std::get_if<StoredPcmBlock>(&reopened);
            if (block == nullptr) {
                fail(audit, {AudioIntegrityIssueKind::block_unreadable, path, std::nullopt, 0, 0,
                             std::get<PcmBlockStoreError>(reopened)});
                return audit;
            }
            if (block_sequence) {
                const auto expected = *block_sequence == (std::numeric_limits<std::uint64_t>::max)()
                                          ? 0
                                          : *block_sequence + 1;
                if (expected == 0 || block->sequence != expected) {
                    fail(audit, {AudioIntegrityIssueKind::block_sequence_discontinuity, path,
                                 block->chunk.range, expected, block->sequence});
                    return audit;
                }
            }
            block_sequence = block->sequence;

            if (previous) {
                if (block->chunk.run_id != previous->run_id ||
                    block->chunk.capture_point != previous->capture_point) {
                    fail(audit, {AudioIntegrityIssueKind::capture_identity_changed, path,
                                 block->chunk.range});
                    return audit;
                }
                if (!same_format(block->chunk.format, previous->format)) {
                    fail(audit,
                         {AudioIntegrityIssueKind::format_changed, path, block->chunk.range});
                    return audit;
                }
                if (block->chunk.range.timeline_id != previous->range.timeline_id ||
                    block->chunk.range.sample_rate != previous->range.sample_rate) {
                    fail(audit,
                         {AudioIntegrityIssueKind::timeline_changed, path, block->chunk.range});
                    return audit;
                }
                switch (join_pcm(*previous, block->chunk, continuity)) {
                case PcmJoin::continues:
                case PcmJoin::starts_segment:
                    break;
                case PcmJoin::sample_gap:
                    fail(audit,
                         {AudioIntegrityIssueKind::sample_gap, path,
                          SampleFrameRange{previous->range.timeline_id, previous->range.sample_rate,
                                           previous->range.end, block->chunk.range.begin}});
                    return audit;
                case PcmJoin::sample_overlap_or_reorder:
                    fail(audit,
                         {AudioIntegrityIssueKind::sample_overlap_or_reorder, path,
                          SampleFrameRange{previous->range.timeline_id, previous->range.sample_rate,
                                           block->chunk.range.begin, previous->range.end}});
                    return audit;
                case PcmJoin::segment_out_of_order:
                    fail(audit, {AudioIntegrityIssueKind::segment_out_of_order, path,
                                 block->chunk.range, previous->segment, block->chunk.segment});
                    return audit;
                }
            }

            for (const auto &effect : block->chunk.discontinuities) {
                if (effect.output_range.availability == Availability::unknown ||
                    (effect.kind == DiscontinuityKind::inserted_silence &&
                     effect.output_range.availability == Availability::not_applicable)) {
                    fail(audit, {AudioIntegrityIssueKind::ambiguous_effect_boundary, path,
                                 effect.affected_range});
                    return audit;
                }
                if (!effect.output_range.value) {
                    continue;
                }
                const auto &output = *effect.output_range.value;
                if (output.timeline_id != block->chunk.range.timeline_id ||
                    output.sample_rate != block->chunk.range.sample_rate ||
                    output.begin < block->chunk.range.begin ||
                    output.end > block->chunk.range.end) {
                    fail(audit, {AudioIntegrityIssueKind::effect_outside_block, path, output});
                    return audit;
                }
                ++audit.verified_effect_boundaries;
            }

            // DI-14: the interval of each segment, as its blocks were verified.
            if (audit.segments.empty() || audit.segments.back().segment != block->chunk.segment)
                audit.segments.push_back({block->chunk.segment, block->chunk.range, 0U});
            auto &segment = audit.segments.back();
            segment.samples.end = block->chunk.range.end;
            ++segment.blocks;

            previous = block->chunk;
            audit.last_verified_sample = block->chunk.range.end;
            ++audit.verified_blocks;
        }
        audit.evidence_result = EvidenceResult::complete;
        return audit;
    } catch (...) {
        fail(audit, {AudioIntegrityIssueKind::block_unreadable});
        return audit;
    }
}

} // namespace ayther::audio_qa
