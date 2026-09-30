#include "exclusive_evidence_directory.h"
#include "fact_fragment_store.h"
#include "fact_integrity.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
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

qa::Fact fact(const std::uint64_t sequence, std::vector<qa::Cause> causes) {
    qa::Fact value;
    value.id = {"run-151", "engine", sequence};
    value.kind = "observed";
    value.frame_index = {qa::Availability::known, sequence, {}};
    value.cause_ids = std::move(causes);
    return value;
}

const qa::ExclusiveEvidenceDirectory &
directory(const qa::ExclusiveEvidenceDirectoryResult &result) {
    const auto *value = std::get_if<qa::ExclusiveEvidenceDirectory>(&result);
    require(value != nullptr, "relationship_fixture_reservation_failed");
    return *value;
}

std::filesystem::path write(const qa::ExclusiveEvidenceDirectory &run, const std::uint64_t sequence,
                            const std::vector<qa::Fact> &facts) {
    const auto result = qa::write_fact_fragment(run, sequence, facts);
    const auto *stored = std::get_if<qa::StoredFactFragment>(&result);
    require(stored != nullptr, "relationship_fragment_write_failed");
    return stored->path;
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-151-relations";
    remove_tree(fixture);
    try {
        const auto forward_result =
            qa::create_exclusive_evidence_directory(fixture / "forward", "run");
        const auto &forward = directory(forward_result);
        const qa::FactId later{"run-151", "engine", 2};
        const std::vector forward_paths{
            write(forward, 1, {fact(1, {later})}),
            write(forward, 2, {fact(2, {qa::PreexistingContext{"recording-start"}})})};
        const auto resolved = qa::audit_fact_integrity(forward_paths);
        require(resolved.evidence_result == qa::EvidenceResult::complete && !resolved.first_issue &&
                    resolved.resolved_internal_causes == 1 &&
                    resolved.unresolved_internal_causes == 0,
                "later_batch_cause_was_not_resolved_at_close");

        const auto missing_result =
            qa::create_exclusive_evidence_directory(fixture / "missing", "run");
        const auto &missing = directory(missing_result);
        const qa::FactId absent{"run-151", "detector", 9};
        const std::vector missing_paths{write(missing, 1, {fact(1, {absent})})};
        const auto unresolved = qa::audit_fact_integrity(missing_paths);
        require(unresolved.evidence_result == qa::EvidenceResult::incomplete &&
                    unresolved.first_issue &&
                    unresolved.first_issue->kind ==
                        qa::FactIntegrityIssueKind::unresolved_internal_cause &&
                    unresolved.first_issue->fact_id == fact(1, {absent}).id &&
                    unresolved.first_issue->related_fact_id == absent &&
                    unresolved.resolved_internal_causes == 0 &&
                    unresolved.unresolved_internal_causes == 1,
                "missing_internal_cause_was_not_preserved_as_gap");

        remove_tree(fixture);
        std::puts("fact_relationship_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "fact_relationship_test: %s\n", error.what());
        return 1;
    }
}
