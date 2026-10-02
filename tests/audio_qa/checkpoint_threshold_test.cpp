#include "checkpoint_store.h"
#include "checkpoint_threshold.h"
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
    fact.id = {"run-142", "runtime", 1};
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
    require(static_cast<bool>(input), "fixture_open_failed");
    const auto size = input.tellg();
    require(size >= 0, "fixture_size_failed");
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    input.seekg(0);
    require(static_cast<bool>(input.read(reinterpret_cast<char *>(result.data()), size)),
            "fixture_read_failed");
    return result;
}

const qa::StoredCheckpoint &stored(const qa::CheckpointStoreResult &result) {
    const auto *value = std::get_if<qa::StoredCheckpoint>(&result);
    require(value != nullptr, "checkpoint_publication_failed");
    return *value;
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-142-threshold";
    remove_tree(fixture);
    try {
        const auto reserved = qa::create_exclusive_evidence_directory(fixture, "run-142");
        const auto fragment_result =
            qa::write_fact_fragment(directory(reserved), 1, std::vector{sample_fact()});
        const auto *fragment = std::get_if<qa::StoredFactFragment>(&fragment_result);
        require(fragment != nullptr, "fact_fragment_fixture_failed");
        const auto durable_result =
            qa::publish_durable_file(fragment->path, read_bytes(fragment->path));
        const auto *durable = std::get_if<qa::DurablePublishedFile>(&durable_result);
        require(durable != nullptr, "durable_fragment_fixture_failed");
        const std::vector artifacts{
            qa::DurableArtifactPublication{qa::CheckpointArtifactKind::fact_fragment, 1, *durable}};
        const auto first_result =
            qa::write_checkpoint(directory(reserved), "checkpoint-1", 1, artifacts);
        const auto &first = stored(first_result);

        qa::CheckpointThreshold threshold{0};
        require(threshold.record_received(64, 10), "initial_bytes_were_rejected");
        const auto before_time = threshold.request_publication(1'009);
        require(before_time.action == qa::CheckpointPublicationAction::none &&
                    before_time.pending_bytes == 64 && threshold.durable_bytes() == 0,
                "checkpoint_was_requested_before_the_time_threshold");
        const auto by_time = threshold.request_publication(1'010);
        require(by_time.action == qa::CheckpointPublicationAction::publish &&
                    by_time.time_threshold && !by_time.byte_threshold &&
                    by_time.publication_bytes == 64 && threshold.publication_pending() &&
                    threshold.durable_bytes() == 0,
                "time_threshold_did_not_request_publication");
        require(threshold.confirm_publication(first, 1'011) && threshold.durable_bytes() == 64 &&
                    threshold.last_checkpoint_sequence() == 1,
                "durability_was_not_confirmed_by_the_checkpoint");

        const auto second_total = 64 + qa::checkpoint_pending_byte_limit;
        require(threshold.record_received(second_total, 1'012),
                "byte_threshold_input_was_rejected");
        const auto by_size = threshold.request_publication(1'012);
        require(by_size.action == qa::CheckpointPublicationAction::publish &&
                    !by_size.time_threshold && by_size.byte_threshold &&
                    by_size.publication_bytes == second_total && threshold.durable_bytes() == 64,
                "byte_threshold_did_not_request_without_announcing_durability");
        const auto second_result =
            qa::write_checkpoint(directory(reserved), "checkpoint-2", 2, artifacts);
        const auto &second = stored(second_result);
        require(threshold.confirm_publication(second, 1'013) &&
                    threshold.durable_bytes() == second_total,
                "second_checkpoint_did_not_advance_durability");

        require(threshold.record_received(second_total + 1, 1'014),
                "post_checkpoint_input_was_rejected");
        const auto next_by_time = threshold.request_publication(2'014);
        require(next_by_time.action == qa::CheckpointPublicationAction::publish &&
                    next_by_time.time_threshold && threshold.durable_bytes() == second_total,
                "pending_data_was_announced_durable_before_flush");
        require(
            threshold.record_received(second_total + 1 + qa::checkpoint_pending_byte_limit, 2'015),
            "in_flight_input_was_rejected");
        const auto saturated = threshold.request_publication(2'015);
        require(saturated.action == qa::CheckpointPublicationAction::saturated &&
                    saturated.byte_threshold && threshold.durable_bytes() == second_total,
                "overlapping_threshold_was_not_reported_as_saturated");
        require(!threshold.confirm_publication(first, 2'016) &&
                    threshold.durable_bytes() == second_total,
                "stale_checkpoint_announced_new_durability");
        require(!threshold.record_received(second_total, 2'017),
                "decreasing_received_counter_was_accepted");

        remove_tree(fixture);
        std::puts("checkpoint_threshold_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "checkpoint_threshold_test: %s\n", error.what());
        return 1;
    }
}
