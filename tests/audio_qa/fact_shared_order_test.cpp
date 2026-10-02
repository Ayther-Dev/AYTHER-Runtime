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

qa::Fact independent(const std::uint64_t sequence, std::string kind) {
    qa::Fact value;
    value.id = {"run-152", "engine", sequence};
    value.kind = std::move(kind);
    value.frame_index = {qa::Availability::known, sequence, {}};
    value.shared_state_order = {qa::Availability::not_applicable, std::nullopt, "independent"};
    return value;
}

qa::Fact ordered(const std::uint64_t fact_sequence, const std::uint64_t state_sequence) {
    auto value = independent(fact_sequence, "shared-change");
    value.shared_state_order = {qa::Availability::known,
                                std::vector<qa::SharedStateOrder>{{"mix-state", state_sequence}},
                                {}};
    return value;
}

const qa::ExclusiveEvidenceDirectory &
directory(const qa::ExclusiveEvidenceDirectoryResult &result) {
    const auto *value = std::get_if<qa::ExclusiveEvidenceDirectory>(&result);
    require(value != nullptr, "shared_order_fixture_reservation_failed");
    return *value;
}

std::filesystem::path write(const qa::ExclusiveEvidenceDirectory &run,
                            const std::vector<qa::Fact> &facts) {
    const auto result = qa::write_fact_fragment(run, 1, facts);
    const auto *stored = std::get_if<qa::StoredFactFragment>(&result);
    require(stored != nullptr, "shared_order_fragment_write_failed");
    return stored->path;
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-152-order";
    remove_tree(fixture);
    try {
        const auto independent_result =
            qa::create_exclusive_evidence_directory(fixture / "independent", "run");
        const auto &independent_run = directory(independent_result);
        const std::vector independent_paths{
            write(independent_run, {independent(1, "detector"), independent(2, "audio-output")})};
        const auto independent_audit = qa::audit_fact_integrity(independent_paths);
        require(independent_audit.evidence_result == qa::EvidenceResult::complete &&
                    !independent_audit.first_issue && independent_audit.independent_facts == 2 &&
                    independent_audit.validated_shared_state_orders == 0,
                "independent_facts_received_invented_causality");

        const auto missing_result =
            qa::create_exclusive_evidence_directory(fixture / "missing-order", "run");
        const auto &missing_run = directory(missing_result);
        const std::vector missing_paths{write(missing_run, {ordered(1, 4), ordered(2, 6)})};
        const auto missing_audit = qa::audit_fact_integrity(missing_paths);
        require(missing_audit.evidence_result == qa::EvidenceResult::incomplete &&
                    missing_audit.first_issue &&
                    missing_audit.first_issue->kind ==
                        qa::FactIntegrityIssueKind::shared_state_order_discontinuity &&
                    missing_audit.first_issue->state_id == "mix-state" &&
                    missing_audit.first_issue->expected_sequence == 5 &&
                    missing_audit.first_issue->observed_sequence == 6,
                "lost_shared_state_order_was_not_incomplete");

        remove_tree(fixture);
        std::puts("fact_shared_order_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "fact_shared_order_test: %s\n", error.what());
        return 1;
    }
}
