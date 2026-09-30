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
                     const std::uint64_t end) {
    qa::AudioChunk value;
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

        remove_tree(fixture);
        std::puts("audio_integrity_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "audio_integrity_test: %s\n", error.what());
        return 1;
    }
}
