#pragma once

#include "model.h"
#include "pcm_block_store.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace ayther::audio_qa {

enum class AudioIntegrityIssueKind {
    empty_capture,
    too_many_blocks,
    block_unreadable,
    block_sequence_discontinuity,
    capture_identity_changed,
    format_changed,
    timeline_changed,
    sample_gap,
    sample_overlap_or_reorder,
    ambiguous_effect_boundary,
    effect_outside_block,
    // DI-14: a block of an earlier segment after one of a later segment.
    segment_out_of_order,
};

struct AudioIntegrityIssue {
    // clang-format off
    AudioIntegrityIssue(AudioIntegrityIssueKind issue_kind,
                        std::filesystem::path block_path = {},
                        std::optional<SampleFrameRange> range = std::nullopt,
                        std::uint64_t expected = 0, std::uint64_t observed = 0,
                        std::optional<PcmBlockStoreError> error = std::nullopt)
        : kind(issue_kind), block(std::move(block_path)), affected_range(range),
          expected_sequence(expected), observed_sequence(observed), store_error(error) {}
    // clang-format on

    AudioIntegrityIssueKind kind{AudioIntegrityIssueKind::empty_capture};
    std::filesystem::path block;
    std::optional<SampleFrameRange> affected_range;
    std::uint64_t expected_sequence{};
    std::uint64_t observed_sequence{};
    std::optional<PcmBlockStoreError> store_error;
    bool operator==(const AudioIntegrityIssue &) const = default;
};

// Spec 002, DI-14 (contracts.md C2, evidence 1.2): how continuous the PCM of a run must be.
enum class PcmContinuity {
    // A linear traversal: one interval, contiguous from the first block to the last, whatever
    // segment each block names.
    single,
    // An inspection: one interval per linear segment. Within a segment the PCM is contiguous;
    // between two segments the line may jump (a navigation produced silently in between), and
    // that is not a loss.
    per_segment,
};

// How the block `next` follows `previous` on the same capture, format and timeline.
enum class PcmJoin {
    continues,
    starts_segment,
    sample_gap,
    sample_overlap_or_reorder,
    segment_out_of_order,
};

[[nodiscard]] PcmJoin join_pcm(const AudioChunk &previous, const AudioChunk &next,
                               PcmContinuity continuity) noexcept;

// The PCM of one segment, as it was kept: its sample interval and its blocks.
struct PcmSegmentInterval {
    std::uint64_t segment{};
    SampleFrameRange samples;
    std::uint64_t blocks{};
    bool operator==(const PcmSegmentInterval &) const = default;
};

struct AudioIntegrityAudit {
    EvidenceResult evidence_result{EvidenceResult::incomplete};
    std::size_t verified_blocks{};
    std::size_t verified_effect_boundaries{};
    std::optional<std::uint64_t> last_verified_sample;
    std::optional<AudioIntegrityIssue> first_issue;
    // DI-14: the verified intervals, one per segment in the order of the blocks.
    std::vector<PcmSegmentInterval> segments;
    bool operator==(const AudioIntegrityAudit &) const = default;
};

[[nodiscard]] AudioIntegrityAudit
audit_audio_integrity(std::span<const std::filesystem::path> block_paths,
                      PcmContinuity continuity = PcmContinuity::single) noexcept;

} // namespace ayther::audio_qa
