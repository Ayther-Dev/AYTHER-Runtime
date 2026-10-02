#pragma once

#include "model.h"
#include "pcm_block_store.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <utility>

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
};

struct AudioIntegrityIssue {
    AudioIntegrityIssue(AudioIntegrityIssueKind issue_kind,
                        std::filesystem::path block_path = {},
                        std::optional<SampleFrameRange> range = std::nullopt,
                        std::uint64_t expected = 0, std::uint64_t observed = 0,
                        std::optional<PcmBlockStoreError> error = std::nullopt)
        : kind(issue_kind), block(std::move(block_path)), affected_range(range),
          expected_sequence(expected), observed_sequence(observed), store_error(error) {}

    AudioIntegrityIssueKind kind{AudioIntegrityIssueKind::empty_capture};
    std::filesystem::path block;
    std::optional<SampleFrameRange> affected_range;
    std::uint64_t expected_sequence{};
    std::uint64_t observed_sequence{};
    std::optional<PcmBlockStoreError> store_error;
    bool operator==(const AudioIntegrityIssue &) const = default;
};

struct AudioIntegrityAudit {
    EvidenceResult evidence_result{EvidenceResult::incomplete};
    std::size_t verified_blocks{};
    std::size_t verified_effect_boundaries{};
    std::optional<std::uint64_t> last_verified_sample;
    std::optional<AudioIntegrityIssue> first_issue;
    bool operator==(const AudioIntegrityAudit &) const = default;
};

[[nodiscard]] AudioIntegrityAudit
audit_audio_integrity(std::span<const std::filesystem::path> block_paths) noexcept;

} // namespace ayther::audio_qa
