#include "checkpoint_store.h"
#include "durable_file.h"
#include "exclusive_evidence_directory.h"
#include "fact_fragment_store.h"
#include "request_ledger.h"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
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

std::string read_text(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "schema_fixture_open_failed");
    const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    require(!input.bad(), "schema_fixture_read_failed");
    return text;
}

void write_text(const std::filesystem::path &path, const std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "schema_fixture_write_open_failed");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    require(static_cast<bool>(output), "schema_fixture_write_failed");
}

std::string replace_once(std::string text, const std::string_view expected,
                         const std::string_view replacement) {
    const auto position = text.find(expected);
    require(position != std::string::npos, "schema_fixture_field_not_found");
    text.replace(position, expected.size(), replacement);
    return text;
}

qa::Request request() {
    return {"request-146", "session-146",          "conditions-a",
            {"take-main"}, qa::Admission::pending, std::nullopt};
}

qa::Run run() {
    qa::Run value;
    value.run_id = "run-146";
    value.request_id = "request-146";
    value.take_id = "take-main";
    return value;
}

qa::Fact sample_fact() {
    qa::Fact fact;
    fact.id = {"run-146", "runtime", 1};
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

qa::RequestLedger take_ledger(qa::RequestLedgerOpenResult result) {
    auto *ledger = std::get_if<qa::RequestLedger>(&result);
    require(ledger != nullptr, "request_ledger_fixture_failed");
    return std::move(*ledger);
}

const qa::ExclusiveEvidenceDirectory &
directory(const qa::ExclusiveEvidenceDirectoryResult &result) {
    const auto *value = std::get_if<qa::ExclusiveEvidenceDirectory>(&result);
    require(value != nullptr, "evidence_directory_fixture_failed");
    return *value;
}

template <class Result, class Error>
void require_error(const Result &result, const Error expected, const char *const message) {
    const auto *error = std::get_if<Error>(&result);
    require(error != nullptr && *error == expected, message);
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-146-schema";
    remove_tree(fixture);
    try {
        require(std::filesystem::create_directory(fixture), "schema_fixture_failed");
        auto ledger = take_ledger(qa::open_request_ledger(fixture));
        const auto registered = ledger.register_request(request(), run());
        require(std::get_if<qa::RequestRegistrationResult>(&registered) != nullptr,
                "schema_ledger_registration_failed");
        const auto ledger_path = ledger.path();
        const auto valid_ledger = read_text(ledger_path);

        const auto major_ledger =
            replace_once(valid_ledger, "schema_version = 1", "schema_version = 2");
        write_text(ledger_path, major_ledger);
        require_error(qa::open_request_ledger(fixture),
                      qa::RequestLedgerError::incompatible_version,
                      "unknown_ledger_major_was_accepted");
        require(read_text(ledger_path) == major_ledger, "unknown_ledger_major_was_rewritten");

        const auto minor_ledger =
            replace_once(valid_ledger, "schema_minor = 0", "schema_minor = 1");
        write_text(ledger_path, minor_ledger);
        require_error(qa::open_request_ledger(fixture),
                      qa::RequestLedgerError::incompatible_version,
                      "unknown_ledger_minor_was_accepted");
        require(read_text(ledger_path) == minor_ledger, "unknown_ledger_minor_was_rewritten");

        auto extended_ledger = valid_ledger;
        extended_ledger.append("\nfuture_required = true\n");
        write_text(ledger_path, extended_ledger);
        require_error(qa::open_request_ledger(fixture), qa::RequestLedgerError::malformed_document,
                      "unknown_ledger_field_was_ignored");
        require(read_text(ledger_path) == extended_ledger, "unknown_ledger_field_was_rewritten");
        write_text(ledger_path, valid_ledger);

        const auto run_directory_result =
            qa::create_exclusive_evidence_directory(fixture, "run-146");
        const auto &run_directory = directory(run_directory_result);
        const auto fragment_result =
            qa::write_fact_fragment(run_directory, 1, std::vector{sample_fact()});
        const auto *fragment = std::get_if<qa::StoredFactFragment>(&fragment_result);
        require(fragment != nullptr, "schema_fragment_fixture_failed");
        const auto fragment_text = read_text(fragment->path);
        const auto durable_result = qa::publish_durable_file(
            fragment->path,
            {reinterpret_cast<const std::byte *>(fragment_text.data()), fragment_text.size()});
        const auto *durable = std::get_if<qa::DurablePublishedFile>(&durable_result);
        require(durable != nullptr, "schema_fragment_was_not_durable");
        const std::vector artifacts{
            qa::DurableArtifactPublication{qa::CheckpointArtifactKind::fact_fragment, 1, *durable}};
        const auto checkpoint_result =
            qa::write_checkpoint(run_directory, "checkpoint-1", 1, artifacts);
        const auto *checkpoint = std::get_if<qa::StoredCheckpoint>(&checkpoint_result);
        require(checkpoint != nullptr, "schema_checkpoint_fixture_failed");
        const auto checkpoint_path = checkpoint->path();
        const auto valid_checkpoint = read_text(checkpoint_path);

        const auto major_checkpoint =
            replace_once(valid_checkpoint, "schema_version = 1", "schema_version = 2");
        write_text(checkpoint_path, major_checkpoint);
        require_error(qa::read_checkpoint(checkpoint_path),
                      qa::CheckpointStoreError::incompatible_version,
                      "unknown_checkpoint_major_was_accepted");
        require(read_text(checkpoint_path) == major_checkpoint,
                "unknown_checkpoint_major_was_rewritten");

        const auto minor_checkpoint =
            replace_once(valid_checkpoint, "schema_minor = 0", "schema_minor = 1");
        write_text(checkpoint_path, minor_checkpoint);
        require_error(qa::read_checkpoint(checkpoint_path),
                      qa::CheckpointStoreError::incompatible_version,
                      "unknown_checkpoint_minor_was_accepted");
        require(read_text(checkpoint_path) == minor_checkpoint,
                "unknown_checkpoint_minor_was_rewritten");

        auto extended_checkpoint = valid_checkpoint;
        extended_checkpoint.append("\nfuture_required = true\n");
        write_text(checkpoint_path, extended_checkpoint);
        require_error(qa::read_checkpoint(checkpoint_path),
                      qa::CheckpointStoreError::malformed_document,
                      "unknown_checkpoint_field_was_ignored");
        require(read_text(checkpoint_path) == extended_checkpoint,
                "unknown_checkpoint_field_was_rewritten");

        remove_tree(fixture);
        std::puts("evidence_schema_compatibility_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "evidence_schema_compatibility_test: %s\n", error.what());
        return 1;
    }
}
