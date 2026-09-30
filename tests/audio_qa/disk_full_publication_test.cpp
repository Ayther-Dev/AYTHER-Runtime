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

qa::Fact sample_fact() {
    qa::Fact fact;
    fact.id = {"run-148", "runtime", 1};
    fact.kind = "voice_started";
    fact.frame_index = {qa::Availability::known, 100, {}};
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
    require(value != nullptr, "evidence_directory_reservation_failed");
    return *value;
}

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "disk_full_fixture_open_failed");
    const auto size = input.tellg();
    require(size >= 0, "disk_full_fixture_size_failed");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    require(static_cast<bool>(input.read(reinterpret_cast<char *>(bytes.data()), size)),
            "disk_full_fixture_read_failed");
    return bytes;
}

bool has_temporary(const std::filesystem::path &directory) {
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().filename().string().starts_with(".ayther-durable-")) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-148-disk-full";
    remove_tree(fixture);
    try {
        const auto reserved = qa::create_exclusive_evidence_directory(fixture, "run-148");
        const auto &run_directory = directory(reserved);
        const auto fragment_result =
            qa::write_fact_fragment(run_directory, 1, std::vector{sample_fact()});
        const auto *fragment = std::get_if<qa::StoredFactFragment>(&fragment_result);
        require(fragment != nullptr, "disk_full_fragment_fixture_failed");
        const auto durable_result =
            qa::publish_durable_file(fragment->path, read_bytes(fragment->path));
        const auto *durable = std::get_if<qa::DurablePublishedFile>(&durable_result);
        require(durable != nullptr, "disk_full_fragment_was_not_durable");
        const std::vector artifacts{
            qa::DurableArtifactPublication{qa::CheckpointArtifactKind::fact_fragment, 1, *durable}};

        const auto first_result = qa::write_checkpoint(run_directory, "checkpoint-1", 1, artifacts);
        const auto *first = std::get_if<qa::StoredCheckpoint>(&first_result);
        require(first != nullptr, "initial_checkpoint_was_not_published");
        const auto original = read_bytes(first->path());

        const auto exhausted = qa::write_checkpoint(run_directory, "checkpoint-2", 2, artifacts,
                                                    qa::DurablePublicationLimits{1});
        require(std::get_if<qa::CheckpointStoreError>(&exhausted) != nullptr &&
                    std::get<qa::CheckpointStoreError>(exhausted) ==
                        qa::CheckpointStoreError::publish_failed &&
                    read_bytes(first->path()) == original &&
                    !has_temporary(first->path().parent_path()),
                "storage_exhaustion_confirmed_or_changed_unwritten_evidence");
        const auto reopened_result = qa::read_checkpoint(first->path());
        const auto *reopened = std::get_if<qa::StoredCheckpoint>(&reopened_result);
        require(reopened != nullptr && reopened->checkpoint().sequence == 1 &&
                    reopened->checkpoint().checkpoint_id == "checkpoint-1",
                "last_integral_checkpoint_did_not_reopen_after_disk_full");

        remove_tree(fixture);
        std::puts("disk_full_publication_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "disk_full_publication_test: %s\n", error.what());
        return 1;
    }
}
