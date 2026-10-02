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

qa::Fact sample_fact(const std::uint64_t sequence, const char *const kind) {
    qa::Fact fact;
    fact.id = {"run-138", "runtime", sequence};
    fact.kind = kind;
    fact.frame_index = {qa::Availability::known, 240 + sequence, {}};
    fact.cause_ids = {qa::FactId{"run-138", "engine", sequence - 1}};
    fact.decision_id = {qa::Availability::known, "decision-138", {}};
    fact.assignment_id = {qa::Availability::known, "assignment-4", {}};
    fact.occurrence_id = {qa::Availability::known, "occurrence-7", {}};
    fact.reason_code = {qa::Availability::known, "observed", {}};
    fact.shared_state_order = {
        qa::Availability::known, std::vector<qa::SharedStateOrder>{{"audio", sequence}}, {}};
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

const qa::StoredFactFragment &stored(const qa::FactFragmentStoreResult &result,
                                     const char *const message) {
    const auto *value = std::get_if<qa::StoredFactFragment>(&result);
    require(value != nullptr, message);
    return *value;
}

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "fragment_open_failed");
    const auto size = input.tellg();
    require(size >= 0, "fragment_size_failed");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    require(static_cast<bool>(input.read(reinterpret_cast<char *>(bytes.data()), size)),
            "fragment_read_failed");
    return bytes;
}

void write_bytes(const std::filesystem::path &path, const std::vector<std::byte> &bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "fragment_mutation_open_failed");
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(output), "fragment_mutation_write_failed");
}

void expect_error(const qa::FactFragmentStoreResult &result,
                  const qa::FactFragmentStoreError expected, const char *const message) {
    const auto *error = std::get_if<qa::FactFragmentStoreError>(&result);
    require(error != nullptr && *error == expected, message);
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-138-fact-fragment";
    remove_tree(fixture);
    try {
        const auto reserved = qa::create_exclusive_evidence_directory(fixture, "run-138");
        const std::vector facts{sample_fact(8, "candidate_selected"),
                                sample_fact(9, "voice_started")};
        const auto written_result = qa::write_fact_fragment(directory(reserved), 41, facts);
        const auto &written = stored(written_result, "fact_fragment_was_not_written");
        require(written.sequence == 41 && written.record_count == facts.size() &&
                    written.facts == facts &&
                    written.path.filename() == "facts-00000000000000000041.aqf" &&
                    written.payload_identity.byte_size > 0 &&
                    written.document_identity.byte_size == std::filesystem::file_size(written.path),
                "written_fragment_metadata_was_incomplete");

        const auto original = read_bytes(written.path);
        const auto reopened_result = qa::read_fact_fragment(written.path);
        const auto &reopened = stored(reopened_result, "fact_fragment_was_not_reopened");
        require(reopened.sequence == written.sequence &&
                    reopened.record_count == written.record_count && reopened.facts == facts &&
                    reopened.payload_identity == written.payload_identity &&
                    reopened.document_identity == written.document_identity,
                "reopened_fragment_lost_count_or_content");

        expect_error(
            qa::write_fact_fragment(directory(reserved), 41, {sample_fact(10, "replacement")}),
            qa::FactFragmentStoreError::already_exists, "fact_fragment_was_replaced");
        require(read_bytes(written.path) == original, "fragment_collision_changed_original_bytes");

        auto wrong_count = original;
        wrong_count[24] = std::byte{3};
        const auto wrong_count_path = written.path.parent_path() / "wrong-count.aqf";
        write_bytes(wrong_count_path, wrong_count);
        expect_error(qa::read_fact_fragment(wrong_count_path),
                     qa::FactFragmentStoreError::count_mismatch,
                     "modified_record_count_was_accepted");

        auto wrong_sequence = original;
        wrong_sequence[16] = std::byte{42};
        const auto wrong_sequence_path = written.path.parent_path() / "wrong-sequence.aqf";
        write_bytes(wrong_sequence_path, wrong_sequence);
        expect_error(qa::read_fact_fragment(wrong_sequence_path),
                     qa::FactFragmentStoreError::sequence_mismatch,
                     "header_and_payload_sequence_mismatch_was_accepted");

        auto modified = original;
        modified.back() ^= std::byte{1};
        const auto modified_path = written.path.parent_path() / "modified.aqf";
        write_bytes(modified_path, modified);
        expect_error(qa::read_fact_fragment(modified_path),
                     qa::FactFragmentStoreError::hash_mismatch,
                     "single_modified_payload_byte_was_not_detected");

        remove_tree(fixture);
        std::puts("fact_fragment_store_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "fact_fragment_store_test: %s\n", error.what());
        return 1;
    }
}
