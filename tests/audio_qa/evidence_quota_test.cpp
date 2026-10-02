#include "checkpoint_store.h"
#include "durable_file.h"
#include "evidence_quota.h"
#include "exclusive_evidence_directory.h"
#include "fact_fragment_store.h"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
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

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "quota_fixture_open_failed");
    const auto size = input.tellg();
    require(size >= 0, "quota_fixture_size_failed");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    require(static_cast<bool>(input.read(reinterpret_cast<char *>(bytes.data()), size)),
            "quota_fixture_read_failed");
    return bytes;
}

qa::Fact sample_fact() {
    qa::Fact fact;
    fact.id = {"run-186", "runtime", 1};
    fact.kind = "voice_started";
    fact.frame_index = {qa::Availability::known, 10, {}};
    fact.cause_ids = {qa::PreexistingContext{"initial-state"}};
    fact.decision_id = {qa::Availability::known, "decision-1", {}};
    fact.assignment_id = {qa::Availability::known, "assignment-1", {}};
    fact.occurrence_id = {qa::Availability::known, "occurrence-1", {}};
    fact.reason_code = {qa::Availability::known, "selected", {}};
    fact.shared_state_order = {
        qa::Availability::known, std::vector<qa::SharedStateOrder>{{"selection", 1}}, {}};
    return fact;
}

const qa::ExclusiveEvidenceDirectory &
directory(const qa::ExclusiveEvidenceDirectoryResult &result) {
    const auto *value = std::get_if<qa::ExclusiveEvidenceDirectory>(&result);
    require(value != nullptr, "quota_directory_reservation_failed");
    return *value;
}

qa::EvidenceQuotaPermit permit(const qa::EvidenceQuotaCheckResult &result,
                               const char *const message) {
    const auto *value = std::get_if<qa::EvidenceQuotaPermit>(&result);
    require(value != nullptr, message);
    return *value;
}

qa::EvidenceQuotaFailure failure(const qa::EvidenceQuotaCheckResult &result,
                                 const qa::EvidenceQuotaFailureReason reason,
                                 const char *const message) {
    const auto *value = std::get_if<qa::EvidenceQuotaFailure>(&result);
    require(value != nullptr && value->reason == reason &&
                value->evidence_result == qa::EvidenceResult::incomplete &&
                qa::well_formed(value->diagnostic),
            message);
    return *value;
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-186-quota";
    remove_tree(fixture);
    try {
        const auto exact_initial =
            qa::check_initial_evidence_reserve("run-186", qa::minimum_initial_evidence_free_bytes);
        require(permit(exact_initial, "exact_initial_reserve_was_rejected").accounted_bytes == 0,
                "initial_reserve_started_with_accounted_bytes");
        const auto &missing_initial =
            failure(qa::check_initial_evidence_reserve("run-186",
                                                       qa::minimum_initial_evidence_free_bytes - 1),
                    qa::EvidenceQuotaFailureReason::initial_reserve_unavailable,
                    "missing_initial_reserve_was_not_incomplete");
        require(!missing_initial.preserved_checkpoint_sequence &&
                    missing_initial.diagnostic.code == "evidence_initial_reserve_unavailable",
                "initial_reserve_diagnostic_was_not_preserved");

        const auto reserved = qa::create_exclusive_evidence_directory(fixture, "run-186");
        const auto fragment_result =
            qa::write_fact_fragment(directory(reserved), 1, std::vector{sample_fact()});
        const auto *fragment = std::get_if<qa::StoredFactFragment>(&fragment_result);
        require(fragment != nullptr, "quota_fragment_fixture_failed");
        const auto durable_result =
            qa::publish_durable_file(fragment->path, read_bytes(fragment->path));
        const auto *durable = std::get_if<qa::DurablePublishedFile>(&durable_result);
        require(durable != nullptr, "quota_fragment_was_not_durable");
        const std::vector artifacts{
            qa::DurableArtifactPublication{qa::CheckpointArtifactKind::fact_fragment, 1, *durable}};
        const auto checkpoint_result =
            qa::write_checkpoint(directory(reserved), "checkpoint-1", 1, artifacts);
        const auto *checkpoint = std::get_if<qa::StoredCheckpoint>(&checkpoint_result);
        require(checkpoint != nullptr, "quota_checkpoint_fixture_failed");
        const auto checkpoint_before = read_bytes(checkpoint->path());

        qa::EvidenceQuotaUsage usage;
        usage.regular.durable_bytes =
            qa::max_evidence_bytes - qa::evidence_terminal_reserve_bytes - 3;
        usage.regular.temporary_bytes = 1;
        usage.regular.in_flight_bytes = 1;
        usage.last_checkpoint_sequence = 1;
        const auto exact_regular =
            qa::check_evidence_write("run-186", usage, qa::EvidenceWriteClass::regular, 1, 1);
        const auto &regular_permit = permit(exact_regular, "exact_regular_limit_was_rejected");
        require(regular_permit.remaining_regular_bytes == 0 &&
                    regular_permit.remaining_terminal_bytes == qa::evidence_terminal_reserve_bytes,
                "temporary_or_in_flight_bytes_were_not_accounted");

        usage.regular.in_flight_bytes += 1;
        const auto &quota_failure = failure(
            qa::check_evidence_write("run-186", usage, qa::EvidenceWriteClass::regular, 1, 1),
            qa::EvidenceQuotaFailureReason::evidence_limit_reached,
            "regular_quota_excess_was_not_incomplete");
        require(quota_failure.preserved_checkpoint_sequence == 1 &&
                    quota_failure.remaining_regular_bytes == 0 &&
                    quota_failure.remaining_terminal_bytes == qa::evidence_terminal_reserve_bytes &&
                    quota_failure.diagnostic.code == "evidence_quota_reached",
                "quota_failure_lost_checkpoint_or_terminal_reserve");

        qa::EvidenceQuotaUsage terminal_usage;
        terminal_usage.regular.durable_bytes =
            qa::max_evidence_bytes - qa::evidence_terminal_reserve_bytes;
        terminal_usage.terminal.durable_bytes = qa::evidence_terminal_reserve_bytes - 1;
        terminal_usage.last_checkpoint_sequence = 1;
        const auto &full = permit(qa::check_evidence_write("run-186", terminal_usage,
                                                           qa::EvidenceWriteClass::terminal, 1, 1),
                                  "exact_evidence_limit_was_rejected");
        require(full.accounted_bytes == qa::max_evidence_bytes &&
                    full.remaining_regular_bytes == 0 && full.remaining_terminal_bytes == 0,
                "exact_evidence_limit_was_not_accounted");
        terminal_usage.terminal.durable_bytes += 1;
        const auto &terminal_failure =
            failure(qa::check_evidence_write("run-186", terminal_usage,
                                             qa::EvidenceWriteClass::terminal, 1, 1),
                    qa::EvidenceQuotaFailureReason::terminal_reserve_exhausted,
                    "terminal_reserve_excess_was_not_incomplete");
        require(terminal_failure.preserved_checkpoint_sequence == 1,
                "terminal_failure_lost_last_checkpoint");

        qa::EvidenceQuotaUsage disk_usage;
        disk_usage.regular.durable_bytes = 1024;
        disk_usage.last_checkpoint_sequence = 1;
        const auto &disk_failure =
            failure(qa::check_evidence_write("run-186", disk_usage, qa::EvidenceWriteClass::regular,
                                             4096, 4095),
                    qa::EvidenceQuotaFailureReason::storage_unavailable,
                    "missing_write_space_was_not_incomplete");
        require(disk_failure.preserved_checkpoint_sequence == 1 &&
                    disk_failure.diagnostic.code == "evidence_storage_unavailable" &&
                    read_bytes(checkpoint->path()) == checkpoint_before,
                "disk_failure_changed_or_lost_last_checkpoint");
        const auto reopened = qa::read_checkpoint(checkpoint->path());
        const auto *reopened_checkpoint = std::get_if<qa::StoredCheckpoint>(&reopened);
        require(reopened_checkpoint != nullptr && reopened_checkpoint->checkpoint().sequence == 1 &&
                    reopened_checkpoint->checkpoint().checkpoint_id == "checkpoint-1",
                "last_checkpoint_did_not_reopen_after_quota_failure");

        remove_tree(fixture);
        std::puts("evidence_quota_test: initial=10GiB evidence=8GiB "
                  "terminal=1MiB checkpoint=preserved result=incomplete");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "evidence_quota_test: %s\n", error.what());
        return 1;
    }
}
