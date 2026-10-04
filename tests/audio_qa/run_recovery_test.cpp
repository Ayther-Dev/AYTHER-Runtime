#include "checkpoint_store.h"
#include "durable_file.h"
#include "exclusive_evidence_directory.h"
#include "fact_fragment_store.h"
#include "request_ledger.h"
#include "run_recovery.h"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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

qa::Request request() {
    return {"request-144", "session-144",          "conditions-a",
            {"take-main"}, qa::Admission::pending, std::nullopt};
}

qa::Run run() {
    qa::Run value;
    value.run_id = "run-144";
    value.request_id = "request-144";
    value.take_id = "take-main";
    value.phase = qa::Phase::playing;
    value.playback_result = qa::PlaybackResult::in_progress;
    value.evidence_result = qa::EvidenceResult::in_progress;
    value.last_executed_frame = 120;
    value.last_durable_sample = 88'200;
    return value;
}

qa::Fact sample_fact() {
    qa::Fact fact;
    fact.id = {"run-144", "runtime", 1};
    fact.kind = "voice_started";
    fact.frame_index = {qa::Availability::known, 120, {}};
    fact.cause_ids = {qa::PreexistingContext{"initial-state"}};
    fact.decision_id = {qa::Availability::known, "decision-1", {}};
    fact.assignment_id = {qa::Availability::known, "assignment-1", {}};
    fact.occurrence_id = {qa::Availability::known, "occurrence-1", {}};
    fact.reason_code = {qa::Availability::known, "selected", {}};
    fact.shared_state_order = {
        qa::Availability::known, std::vector<qa::SharedStateOrder>{{"selection", 1}}, {}};
    return fact;
}

qa::RequestLedger take_ledger(qa::RequestLedgerOpenResult result) {
    auto *ledger = std::get_if<qa::RequestLedger>(&result);
    require(ledger != nullptr, "request_ledger_open_failed");
    return std::move(*ledger);
}

const qa::ExclusiveEvidenceDirectory &
directory(const qa::ExclusiveEvidenceDirectoryResult &result) {
    const auto *value = std::get_if<qa::ExclusiveEvidenceDirectory>(&result);
    require(value != nullptr, "evidence_directory_reservation_failed");
    return *value;
}

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "recovery_fixture_open_failed");
    const auto size = input.tellg();
    require(size >= 0, "recovery_fixture_size_failed");
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    input.seekg(0);
    require(static_cast<bool>(input.read(reinterpret_cast<char *>(result.data()), size)),
            "recovery_fixture_read_failed");
    return result;
}

const qa::InterruptedRunRecovery &recovery(const qa::RunRecoveryResult &result) {
    const auto *value = std::get_if<qa::InterruptedRunRecovery>(&result);
    require(value != nullptr, "interrupted_run_recovery_failed");
    return *value;
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-144-recovery";
    remove_tree(fixture);
    try {
        require(std::filesystem::create_directory(fixture), "recovery_fixture_failed");
        {
            auto ledger = take_ledger(qa::open_request_ledger(fixture));
            const auto registered = ledger.register_request(request(), run());
            require(std::get_if<qa::RequestRegistrationResult>(&registered) != nullptr,
                    "interrupted_run_was_not_registered");
        }

        const auto run_directory_result =
            qa::create_exclusive_evidence_directory(fixture, "run-144");
        const auto &run_directory = directory(run_directory_result);
        const auto fragment_result =
            qa::write_fact_fragment(run_directory, 1, std::vector{sample_fact()});
        const auto *fragment = std::get_if<qa::StoredFactFragment>(&fragment_result);
        require(fragment != nullptr, "recovery_fragment_failed");
        const auto published_result =
            qa::publish_durable_file(fragment->path, read_bytes(fragment->path));
        const auto *published = std::get_if<qa::DurablePublishedFile>(&published_result);
        require(published != nullptr, "recovery_fragment_was_not_durable");
        const std::vector artifacts{qa::DurableArtifactPublication{
            qa::CheckpointArtifactKind::fact_fragment, 1, *published}};
        const auto checkpoint_result =
            qa::write_checkpoint(run_directory, "checkpoint-1", 1, artifacts);
        const auto *checkpoint = std::get_if<qa::StoredCheckpoint>(&checkpoint_result);
        require(checkpoint != nullptr, "recovery_checkpoint_failed");

        auto reopened = take_ledger(qa::open_request_ledger(fixture));
        const auto recovered =
            qa::recover_interrupted_run(reopened, "request-144", checkpoint->path());
        const auto &view = recovery(recovered);
        require(view.run.phase == qa::Phase::playing &&
                    view.run.playback_result == qa::PlaybackResult::in_progress &&
                    view.run.evidence_result == qa::EvidenceResult::incomplete &&
                    !view.run.cessation_confirmed && view.run.last_executed_frame == 120 &&
                    view.run.last_durable_sample == 88'200 &&
                    view.checkpoint_state == qa::RecoveredCheckpointState::verified &&
                    view.checkpoint && view.checkpoint->sequence == 1 &&
                    !view.automatic_resume_allowed && reopened.size() == 1,
                "interrupted_run_recovery_changed_confirmed_boundaries");

        auto verified = take_ledger(qa::open_request_ledger(fixture));
        const auto durable_run = verified.find_run("request-144");
        require(durable_run && durable_run->evidence_result == qa::EvidenceResult::incomplete &&
                    durable_run->phase == qa::Phase::playing && verified.size() == 1 &&
                    verified.generation() == 2,
                "incomplete_recovery_was_not_persisted_without_relaunch");
        const auto repeated =
            qa::recover_interrupted_run(verified, "request-144", checkpoint->path());
        require(recovery(repeated).run.evidence_result == qa::EvidenceResult::incomplete &&
                    !recovery(repeated).automatic_resume_allowed && verified.generation() == 2,
                "repeated_recovery_changed_the_durable_run");

        remove_tree(fixture);
        std::puts("run_recovery_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "run_recovery_test: %s\n", error.what());
        return 1;
    }
}
