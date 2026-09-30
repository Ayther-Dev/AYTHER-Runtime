#include "exclusive_evidence_directory.h"
#include "fact_fragment_store.h"
#include "fact_integrity.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
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

qa::Fact fact(const std::uint64_t sequence, std::string kind = "observed") {
    qa::Fact value;
    value.id = {"run-150", "engine", sequence};
    value.kind = std::move(kind);
    value.frame_index = {qa::Availability::known, sequence, {}};
    return value;
}

const qa::ExclusiveEvidenceDirectory &
directory(const qa::ExclusiveEvidenceDirectoryResult &result) {
    const auto *value = std::get_if<qa::ExclusiveEvidenceDirectory>(&result);
    require(value != nullptr, "integrity_fixture_reservation_failed");
    return *value;
}

std::filesystem::path write(const qa::ExclusiveEvidenceDirectory &run,
                            const std::uint64_t fragment_sequence,
                            const std::vector<qa::Fact> &facts) {
    const auto result = qa::write_fact_fragment(run, fragment_sequence, facts);
    const auto *stored = std::get_if<qa::StoredFactFragment>(&result);
    require(stored != nullptr, "integrity_fragment_write_failed");
    return stored->path;
}

void expect_issue(const qa::FactIntegrityAudit &audit, const qa::FactIntegrityIssueKind kind,
                  const std::uint64_t expected, const std::uint64_t observed,
                  const char *const message) {
    require(audit.evidence_result == qa::EvidenceResult::incomplete && audit.first_issue &&
                audit.first_issue->kind == kind &&
                audit.first_issue->expected_sequence == expected &&
                audit.first_issue->observed_sequence == observed,
            message);
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-150-integrity";
    remove_tree(fixture);
    try {
        const auto complete_result =
            qa::create_exclusive_evidence_directory(fixture / "complete", "run");
        const auto &complete = directory(complete_result);
        const auto first = fact(10);
        const std::vector complete_paths{write(complete, 20, {first}),
                                         write(complete, 21, {first, fact(11)})};
        const auto complete_audit = qa::audit_fact_integrity(complete_paths);
        require(complete_audit.evidence_result == qa::EvidenceResult::complete &&
                    !complete_audit.first_issue && complete_audit.verified_fragments == 2 &&
                    complete_audit.unique_facts == 2 && complete_audit.identical_duplicates == 1,
                "identical_retransmission_changed_complete_segment");

        const auto producer_gap_result =
            qa::create_exclusive_evidence_directory(fixture / "producer-gap", "run");
        const auto &producer_gap = directory(producer_gap_result);
        const std::vector producer_gap_paths{write(producer_gap, 1, {fact(30)}),
                                             write(producer_gap, 2, {fact(32)})};
        expect_issue(qa::audit_fact_integrity(producer_gap_paths),
                     qa::FactIntegrityIssueKind::producer_sequence_discontinuity, 31, 32,
                     "producer_sequence_gap_was_not_incomplete");

        const auto fragment_gap_result =
            qa::create_exclusive_evidence_directory(fixture / "fragment-gap", "run");
        const auto &fragment_gap = directory(fragment_gap_result);
        const std::vector fragment_gap_paths{write(fragment_gap, 7, {fact(40)}),
                                             write(fragment_gap, 9, {fact(41)})};
        expect_issue(qa::audit_fact_integrity(fragment_gap_paths),
                     qa::FactIntegrityIssueKind::fragment_sequence_discontinuity, 8, 9,
                     "fragment_sequence_gap_was_not_incomplete");

        const auto conflict_result =
            qa::create_exclusive_evidence_directory(fixture / "conflict", "run");
        const auto &conflict = directory(conflict_result);
        const std::vector conflict_paths{write(conflict, 1, {fact(50, "candidate")}),
                                         write(conflict, 2, {fact(50, "selection")})};
        const auto conflict_audit = qa::audit_fact_integrity(conflict_paths);
        expect_issue(conflict_audit, qa::FactIntegrityIssueKind::conflicting_fact_id, 50, 50,
                     "conflicting_fact_id_was_not_incomplete");
        require(conflict_audit.first_issue->fact_id == fact(50).id,
                "conflicting_fact_id_was_not_identified");

        remove_tree(fixture);
        std::puts("fact_integrity_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "fact_integrity_test: %s\n", error.what());
        return 1;
    }
}
