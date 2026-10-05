#include "audio_integrity.h"
#include "content_hash.h"
#include "exclusive_evidence_directory.h"
#include "pcm_block_store.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void remove_tree(const std::filesystem::path &path) {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

qa::AudioChunk chunk(const std::uint64_t producer_sequence, const std::uint64_t begin,
                     const std::uint64_t end, const std::uint64_t segment = 0) {
    qa::AudioChunk value;
    value.segment = segment;
    value.run_id = "run-153";
    value.capture_point = "session-postmix";
    value.producer_sequence = producer_sequence;
    value.format = {qa::PcmFormat::s16le, 48000, 2};
    value.range = {"main-output", 48000, begin, end};
    const auto size = qa::expected_payload_bytes(value.format, end - begin);
    require(size.has_value(), "audio_integrity_fixture_size_failed");
    value.bytes.resize(*size);
    value.sha256 = {qa::Availability::known, qa::identify_content(value.bytes).sha256, {}};
    value.checkpoint_id = {qa::Availability::not_applicable, std::nullopt, "pending"};
    return value;
}

const qa::ExclusiveEvidenceDirectory &
directory(const qa::ExclusiveEvidenceDirectoryResult &result) {
    const auto *value = std::get_if<qa::ExclusiveEvidenceDirectory>(&result);
    require(value != nullptr, "audio_integrity_fixture_reservation_failed");
    return *value;
}

std::filesystem::path write(const qa::ExclusiveEvidenceDirectory &run, const std::uint64_t sequence,
                            const qa::AudioChunk &value) {
    const auto result = qa::write_pcm_block(run, sequence, value);
    const auto *stored = std::get_if<qa::StoredPcmBlock>(&result);
    require(stored != nullptr, "audio_integrity_block_write_failed");
    return stored->path;
}

void expect_range(const qa::AudioIntegrityAudit &audit, const qa::AudioIntegrityIssueKind kind,
                  const qa::SampleFrameRange &range, const char *const message) {
    require(audit.evidence_result == qa::EvidenceResult::incomplete && audit.first_issue &&
                audit.first_issue->kind == kind && audit.first_issue->affected_range &&
                *audit.first_issue->affected_range == range,
            message);
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-153-audio";
    remove_tree(fixture);
    try {
        const auto complete_result =
            qa::create_exclusive_evidence_directory(fixture / "complete", "run");
        const auto &complete = directory(complete_result);
        auto first = chunk(1, 0, 4);
        first.discontinuities.push_back(
            {qa::DiscontinuityKind::inserted_silence,
             {"synth-input", 44100, 10, 12},
             {qa::Availability::known, qa::SampleFrameRange{"main-output", 48000, 1, 3}, {}},
             {}});
        const std::vector complete_paths{write(complete, 10, first),
                                         write(complete, 11, chunk(2, 4, 8))};
        const auto complete_audit = qa::audit_audio_integrity(complete_paths);
        require(complete_audit.evidence_result == qa::EvidenceResult::complete &&
                    !complete_audit.first_issue && complete_audit.verified_blocks == 2 &&
                    complete_audit.verified_effect_boundaries == 1 &&
                    complete_audit.last_verified_sample == 8,
                "contiguous_audio_was_not_verified");

        const auto gap_result = qa::create_exclusive_evidence_directory(fixture / "gap", "run");
        const auto &gap = directory(gap_result);
        const std::vector gap_paths{write(gap, 1, chunk(1, 0, 4)), write(gap, 2, chunk(2, 6, 8))};
        expect_range(qa::audit_audio_integrity(gap_paths), qa::AudioIntegrityIssueKind::sample_gap,
                     {"main-output", 48000, 4, 6}, "missing_sample_interval_was_not_reported");

        const auto overlap_result =
            qa::create_exclusive_evidence_directory(fixture / "overlap", "run");
        const auto &overlap = directory(overlap_result);
        const std::vector overlap_paths{write(overlap, 1, chunk(1, 0, 4)),
                                        write(overlap, 2, chunk(2, 2, 6))};
        expect_range(qa::audit_audio_integrity(overlap_paths),
                     qa::AudioIntegrityIssueKind::sample_overlap_or_reorder,
                     {"main-output", 48000, 2, 4}, "overlapping_sample_interval_was_not_reported");

        const auto ambiguous_result =
            qa::create_exclusive_evidence_directory(fixture / "ambiguous", "run");
        const auto &ambiguous = directory(ambiguous_result);
        auto ambiguous_chunk = chunk(1, 0, 4);
        ambiguous_chunk.discontinuities.push_back(
            {qa::DiscontinuityKind::inserted_silence,
             {"synth-input", 44100, 20, 22},
             {qa::Availability::unknown, std::nullopt, "boundary_lost"},
             {}});
        const std::vector ambiguous_paths{write(ambiguous, 1, ambiguous_chunk)};
        expect_range(qa::audit_audio_integrity(ambiguous_paths),
                     qa::AudioIntegrityIssueKind::ambiguous_effect_boundary,
                     {"synth-input", 44100, 20, 22}, "ambiguous_effect_boundary_was_not_reported");

        // Spec 002, DI-14: an inspection keeps its PCM per linear segment. A step back restores a
        // checkpoint and the line goes back (segment 1 starts at 2); a step forward produces a
        // frame silently and the line jumps (segment 2 starts at 20). Neither is a loss.
        const auto segmented_result =
            qa::create_exclusive_evidence_directory(fixture / "segmented", "run");
        const auto &segmented = directory(segmented_result);
        const std::vector segmented_paths{
            write(segmented, 1, chunk(1, 0, 4)), write(segmented, 2, chunk(2, 4, 8)),
            write(segmented, 3, chunk(3, 2, 6, 1)), write(segmented, 4, chunk(4, 20, 24, 2))};
        const auto per_segment =
            qa::audit_audio_integrity(segmented_paths, qa::PcmContinuity::per_segment);
        require(per_segment.evidence_result == qa::EvidenceResult::complete &&
                    !per_segment.first_issue && per_segment.verified_blocks == 4,
                "DI-14: a jump between two segments of an inspection is not a loss");
        require(per_segment.segments ==
                    std::vector<qa::PcmSegmentInterval>{{0, {"main-output", 48000, 0, 8}, 2},
                                                        {1, {"main-output", 48000, 2, 6}, 1},
                                                        {2, {"main-output", 48000, 20, 24}, 1}},
                "DI-14: each segment declares its sample interval and its blocks");
        // A linear traversal is one continuous interval, whatever the segments say.
        expect_range(qa::audit_audio_integrity(segmented_paths),
                     qa::AudioIntegrityIssueKind::sample_overlap_or_reorder,
                     {"main-output", 48000, 2, 8},
                     "DI-14: a linear traversal still requires one continuous interval");

        // Within a segment, continuity is audited as before.
        const auto inner_gap_result =
            qa::create_exclusive_evidence_directory(fixture / "inner-gap", "run");
        const auto &inner_gap = directory(inner_gap_result);
        const std::vector inner_gap_paths{write(inner_gap, 1, chunk(1, 0, 4)),
                                          write(inner_gap, 2, chunk(2, 2, 6, 1)),
                                          write(inner_gap, 3, chunk(3, 7, 9, 1))};
        expect_range(qa::audit_audio_integrity(inner_gap_paths, qa::PcmContinuity::per_segment),
                     qa::AudioIntegrityIssueKind::sample_gap, {"main-output", 48000, 6, 7},
                     "DI-14: a gap inside a segment is a loss");
        const auto reordered_result =
            qa::create_exclusive_evidence_directory(fixture / "reordered", "run");
        const auto &reordered = directory(reordered_result);
        const std::vector reordered_paths{write(reordered, 1, chunk(1, 0, 4, 1)),
                                          write(reordered, 2, chunk(2, 4, 8))};
        const auto reordered_audit =
            qa::audit_audio_integrity(reordered_paths, qa::PcmContinuity::per_segment);
        require(reordered_audit.evidence_result == qa::EvidenceResult::incomplete &&
                    reordered_audit.first_issue &&
                    reordered_audit.first_issue->kind ==
                        qa::AudioIntegrityIssueKind::segment_out_of_order,
                "DI-14: a segment never comes back");

        // The rule itself, block to block.
        using Join = qa::PcmJoin;
        const auto per = qa::PcmContinuity::per_segment;
        const auto single = qa::PcmContinuity::single;
        require(qa::join_pcm(chunk(1, 0, 4), chunk(2, 4, 8), per) == Join::continues &&
                    qa::join_pcm(chunk(1, 0, 4), chunk(2, 6, 8), per) == Join::sample_gap &&
                    qa::join_pcm(chunk(1, 0, 4), chunk(2, 2, 8), per) ==
                        Join::sample_overlap_or_reorder &&
                    qa::join_pcm(chunk(1, 0, 4), chunk(2, 2, 8, 1), per) == Join::starts_segment &&
                    qa::join_pcm(chunk(1, 0, 4), chunk(2, 9, 12, 1), per) == Join::starts_segment &&
                    qa::join_pcm(chunk(1, 0, 4, 1), chunk(2, 4, 8), per) ==
                        Join::segment_out_of_order,
                "DI-14: within a segment contiguous, between segments free, never backwards");
        require(qa::join_pcm(chunk(1, 0, 4), chunk(2, 4, 8, 1), single) == Join::starts_segment &&
                    qa::join_pcm(chunk(1, 0, 4), chunk(2, 2, 8, 1), single) ==
                        Join::sample_overlap_or_reorder &&
                    qa::join_pcm(chunk(1, 0, 4), chunk(2, 9, 12, 1), single) == Join::sample_gap,
                "DI-14: a linear traversal joins its segments without a jump");

        remove_tree(fixture);
        std::puts("audio_integrity_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "audio_integrity_test: %s\n", error.what());
        return 1;
    }
}
