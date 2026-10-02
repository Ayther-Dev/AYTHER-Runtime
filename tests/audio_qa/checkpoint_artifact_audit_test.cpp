#include "checkpoint_store.h"
#include "durable_file.h"
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

qa::Fact sample_fact(const std::uint64_t sequence) {
    qa::Fact fact;
    fact.id = {"run-145", "runtime", sequence};
    fact.kind = "voice_started";
    fact.frame_index = {qa::Availability::known, 100 + sequence, {}};
    fact.cause_ids = {qa::PreexistingContext{"initial-state"}};
    fact.decision_id = {qa::Availability::known, "decision-" + std::to_string(sequence), {}};
    fact.assignment_id = {qa::Availability::known, "assignment-1", {}};
    fact.occurrence_id = {qa::Availability::known, "occurrence-" + std::to_string(sequence), {}};
    fact.reason_code = {qa::Availability::known, "selected", {}};
    fact.shared_state_order = {
        qa::Availability::known, std::vector<qa::SharedStateOrder>{{"selection", sequence}}, {}};
    return fact;
}

const qa::ExclusiveEvidenceDirectory &
directory(const qa::ExclusiveEvidenceDirectoryResult &result) {
    const auto *value = std::get_if<qa::ExclusiveEvidenceDirectory>(&result);
    require(value != nullptr, "evidence_directory_reservation_failed");
    return *value;
}

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "audit_fixture_open_failed");
    const auto size = input.tellg();
    require(size >= 0, "audit_fixture_size_failed");
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    input.seekg(0);
    require(static_cast<bool>(input.read(reinterpret_cast<char *>(result.data()), size)),
            "audit_fixture_read_failed");
    return result;
}

qa::DurablePublishedFile publish_fragment(const qa::ExclusiveEvidenceDirectory &run_directory,
                                          const std::uint64_t sequence,
                                          std::filesystem::path &path) {
    const auto written =
        qa::write_fact_fragment(run_directory, sequence, std::vector{sample_fact(sequence)});
    const auto *fragment = std::get_if<qa::StoredFactFragment>(&written);
    require(fragment != nullptr, "fact_fragment_fixture_failed");
    path = fragment->path;
    const auto published = qa::publish_durable_file(path, read_bytes(fragment->path));
    const auto *durable = std::get_if<qa::DurablePublishedFile>(&published);
    require(durable != nullptr, "fact_fragment_was_not_durable");
    return *durable;
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-145-audit";
    remove_tree(fixture);
    try {
        const auto reserved = qa::create_exclusive_evidence_directory(fixture, "run-145");
        const auto &run_directory = directory(reserved);
        std::filesystem::path first_path;
        std::filesystem::path second_path;
        const auto first = publish_fragment(run_directory, 1, first_path);
        const auto second = publish_fragment(run_directory, 2, second_path);
        const auto first_bytes = read_bytes(first_path);
        const auto second_bytes = read_bytes(second_path);
        const std::vector artifacts{
            qa::DurableArtifactPublication{qa::CheckpointArtifactKind::fact_fragment, 1, first},
            qa::DurableArtifactPublication{qa::CheckpointArtifactKind::fact_fragment, 2, second}};
        const auto checkpoint_result =
            qa::write_checkpoint(run_directory, "checkpoint-1", 1, artifacts);
        const auto *checkpoint = std::get_if<qa::StoredCheckpoint>(&checkpoint_result);
        require(checkpoint != nullptr, "audit_checkpoint_fixture_failed");

        const auto truncated_size = second_bytes.size() / 2;
        {
            std::ofstream output(second_path, std::ios::binary | std::ios::trunc);
            require(static_cast<bool>(output), "corrupt_fragment_open_failed");
            output.write(reinterpret_cast<const char *>(second_bytes.data()),
                         static_cast<std::streamsize>(truncated_size));
            require(static_cast<bool>(output), "corrupt_fragment_write_failed");
        }
        const auto corrupt_bytes = read_bytes(second_path);
        require(corrupt_bytes.size() == truncated_size, "fragment_was_not_truncated");

        const auto audited = qa::audit_checkpoint_artifacts(checkpoint->path());
        const auto *audit = std::get_if<qa::CheckpointArtifactAudit>(&audited);
        require(audit != nullptr && audit->verified_artifacts.size() == 1 &&
                    audit->verified_artifacts.front().sequence == 1 && audit->first_unverifiable &&
                    audit->first_unverifiable->artifact.sequence == 2 &&
                    audit->first_unverifiable->reason == qa::CheckpointStoreError::artifact_invalid,
                "audit_did_not_identify_the_first_corrupt_fragment");
        const auto reopened_first = qa::read_fact_fragment(first_path);
        const auto reopened_checkpoint = qa::read_checkpoint(checkpoint->path());
        require(read_bytes(first_path) == first_bytes && read_bytes(second_path) == corrupt_bytes &&
                    std::get_if<qa::StoredFactFragment>(&reopened_first) != nullptr &&
                    std::get_if<qa::CheckpointStoreError>(&reopened_checkpoint) != nullptr,
                "audit_changed_original_fragments_or_hid_corruption");

        remove_tree(fixture);
        std::puts("checkpoint_artifact_audit_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "checkpoint_artifact_audit_test: %s\n", error.what());
        return 1;
    }
}
