#include "checkpoint_store.h"
#include "durable_file.h"
#include "exclusive_evidence_directory.h"
#include "fact_fragment_store.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace qa = ayther::audio_qa;
namespace {

static_assert(
    !std::is_constructible_v<qa::DurablePublishedFile, std::filesystem::path, qa::ContentIdentity>);
static_assert(!std::is_constructible_v<qa::StoredCheckpoint, std::filesystem::path,
                                       qa::ContentIdentity, qa::EvidenceCheckpoint>);

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

qa::Fact sample_fact() {
    qa::Fact fact;
    fact.id = {"run-141", "runtime", 8};
    fact.kind = "voice_started";
    fact.frame_index = {qa::Availability::known, 320, {}};
    fact.cause_ids = {qa::PreexistingContext{"initial-state"}};
    fact.decision_id = {qa::Availability::known, "decision-8", {}};
    fact.assignment_id = {qa::Availability::known, "assignment-3", {}};
    fact.occurrence_id = {qa::Availability::known, "occurrence-5", {}};
    fact.reason_code = {qa::Availability::known, "selected", {}};
    fact.shared_state_order = {
        qa::Availability::known, std::vector<qa::SharedStateOrder>{{"selection", 11}}, {}};
    return fact;
}

void remove_tree(const std::filesystem::path &path) {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

const qa::ExclusiveEvidenceDirectory &
directory(const qa::ExclusiveEvidenceDirectoryResult &result) {
    const auto *value = std::get_if<qa::ExclusiveEvidenceDirectory>(&result);
    require(value != nullptr, "evidence_directory_reservation_failed");
    return *value;
}

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "checkpoint_fixture_open_failed");
    const auto size = input.tellg();
    require(size >= 0, "checkpoint_fixture_size_failed");
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    input.seekg(0);
    require(static_cast<bool>(input.read(reinterpret_cast<char *>(result.data()), size)),
            "checkpoint_fixture_read_failed");
    return result;
}

const qa::DurablePublishedFile &published(const qa::DurablePublishResult &result) {
    const auto *value = std::get_if<qa::DurablePublishedFile>(&result);
    require(value != nullptr, "fragment_was_not_published_durably");
    return *value;
}

const qa::StoredCheckpoint &stored(const qa::CheckpointStoreResult &result,
                                   const char *const message) {
    const auto *value = std::get_if<qa::StoredCheckpoint>(&result);
    require(value != nullptr, message);
    return *value;
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-141-checkpoint";
    remove_tree(fixture);
    try {
        const auto reserved = qa::create_exclusive_evidence_directory(fixture, "run-141");
        const auto fragment_result =
            qa::write_fact_fragment(directory(reserved), 41, std::vector{sample_fact()});
        const auto *fragment = std::get_if<qa::StoredFactFragment>(&fragment_result);
        require(fragment != nullptr, "fact_fragment_fixture_failed");
        const auto fragment_bytes = read_bytes(fragment->path);
        const auto durable_result = qa::publish_durable_file(fragment->path, fragment_bytes);
        const auto &durable_fragment = published(durable_result);
        const std::vector artifacts{qa::DurableArtifactPublication{
            qa::CheckpointArtifactKind::fact_fragment, 41, durable_fragment}};

        const auto first_result =
            qa::write_checkpoint(directory(reserved), "checkpoint-1", 1, artifacts);
        const auto &first = stored(first_result, "checkpoint_was_not_published");
        require(first.checkpoint().sequence == 1 &&
                    first.checkpoint().checkpoint_id == "checkpoint-1" &&
                    first.checkpoint().artifacts.size() == 1 &&
                    first.checkpoint().artifacts.front().sequence == 41 &&
                    first.checkpoint().artifacts.front().identity == durable_fragment.identity() &&
                    first.checkpoint().artifacts.front().relative_path ==
                        "fragments/facts-00000000000000000041.aqf",
                "checkpoint_listed_an_unverified_fragment");
        const auto previous_bytes = read_bytes(first.path());
        const auto reopened = qa::read_checkpoint(first.path());
        require(stored(reopened, "checkpoint_was_not_reopened").checkpoint() == first.checkpoint(),
                "reopened_checkpoint_changed_durable_artifacts");

#ifdef _WIN32
        const auto lock =
            CreateFileW(first.path().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(lock != INVALID_HANDLE_VALUE, "checkpoint_lock_failed");
        const auto interrupted =
            qa::write_checkpoint(directory(reserved), "checkpoint-2", 2, artifacts);
        const bool publication_failed =
            std::get_if<qa::CheckpointStoreError>(&interrupted) != nullptr &&
            std::get<qa::CheckpointStoreError>(interrupted) ==
                qa::CheckpointStoreError::publish_failed;
        require(CloseHandle(lock) != 0, "checkpoint_unlock_failed");
        require(
            publication_failed && read_bytes(first.path()) == previous_bytes &&
                stored(qa::read_checkpoint(first.path()), "previous_checkpoint_was_not_preserved")
                        .checkpoint() == first.checkpoint(),
            "interrupted_checkpoint_replaced_previous_version");
#endif

        remove_tree(fixture);
        std::puts("checkpoint_store_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "checkpoint_store_test: %s\n", error.what());
        return 1;
    }
}
