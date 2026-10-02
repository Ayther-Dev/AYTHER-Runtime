#include "content_hash.h"
#include "exclusive_evidence_directory.h"
#include "pcm_block_store.h"
#include "pcm_message.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
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

qa::AudioChunk sample_chunk() {
    qa::AudioChunk chunk;
    chunk.run_id = "run-139";
    chunk.capture_point = "session-postmix";
    chunk.producer_sequence = 17;
    chunk.format = {qa::PcmFormat::s24le, 48000, 2};
    chunk.range = {"captured-output", 48000, 4096, 4100};
    const auto size = qa::expected_payload_bytes(chunk.format, 4);
    require(size.has_value(), "sample_pcm_size_was_rejected");
    chunk.bytes.resize(*size);
    for (std::size_t index{}; index < chunk.bytes.size(); ++index) {
        chunk.bytes[index] = static_cast<std::byte>((index * 37U + 11U) & 0xffU);
    }
    chunk.sha256 = {qa::Availability::known, qa::pcm_sha256(chunk.bytes), {}};
    chunk.durability = qa::Durability::pending;
    chunk.checkpoint_id = {qa::Availability::not_applicable, std::nullopt, "not_published"};
    chunk.cause_ids = {{"run-139", "audio-mixer", 15}};
    qa::AudioDiscontinuity discontinuity;
    discontinuity.kind = qa::DiscontinuityKind::inserted_silence;
    discontinuity.affected_range = {"synth-input", 44100, 512, 516};
    discontinuity.output_range = {
        qa::Availability::known, qa::SampleFrameRange{"captured-output", 48000, 4097, 4099}, {}};
    discontinuity.cause_ids = {{"run-139", "audio-mixer", 16}};
    chunk.discontinuities.push_back(discontinuity);
    return chunk;
}

void require_same(const qa::AudioChunk &expected, const qa::AudioChunk &actual) {
    require(expected.run_id == actual.run_id && expected.capture_point == actual.capture_point &&
                expected.producer_sequence == actual.producer_sequence &&
                expected.format.pcm == actual.format.pcm &&
                expected.format.sample_rate == actual.format.sample_rate &&
                expected.format.channels == actual.format.channels &&
                expected.range == actual.range && expected.bytes == actual.bytes &&
                expected.sha256 == actual.sha256 && expected.durability == actual.durability &&
                expected.checkpoint_id == actual.checkpoint_id &&
                expected.cause_ids == actual.cause_ids &&
                expected.discontinuities.size() == actual.discontinuities.size() &&
                expected.discontinuities.front().kind == actual.discontinuities.front().kind &&
                expected.discontinuities.front().affected_range ==
                    actual.discontinuities.front().affected_range &&
                expected.discontinuities.front().output_range ==
                    actual.discontinuities.front().output_range &&
                expected.discontinuities.front().cause_ids ==
                    actual.discontinuities.front().cause_ids,
            "reopened_pcm_block_changed_metadata_or_samples");
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

const qa::StoredPcmBlock &stored(const qa::PcmBlockStoreResult &result, const char *const message) {
    const auto *value = std::get_if<qa::StoredPcmBlock>(&result);
    require(value != nullptr, message);
    return *value;
}

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "pcm_block_open_failed");
    const auto size = input.tellg();
    require(size >= 0, "pcm_block_size_failed");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    require(static_cast<bool>(input.read(reinterpret_cast<char *>(bytes.data()), size)),
            "pcm_block_read_failed");
    return bytes;
}

void write_bytes(const std::filesystem::path &path, const std::vector<std::byte> &bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "pcm_block_mutation_open_failed");
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(output), "pcm_block_mutation_write_failed");
}

void expect_error(const qa::PcmBlockStoreResult &result, const qa::PcmBlockStoreError expected,
                  const char *const message) {
    const auto *error = std::get_if<qa::PcmBlockStoreError>(&result);
    require(error != nullptr && *error == expected, message);
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-139-pcm-block";
    remove_tree(fixture);
    try {
        const auto reserved = qa::create_exclusive_evidence_directory(fixture, "run-139");
        const auto chunk = sample_chunk();
        const auto written_result = qa::write_pcm_block(directory(reserved), 23, chunk);
        const auto &written = stored(written_result, "pcm_block_was_not_written");
        require(written.sequence == 23 &&
                    written.path.filename() == "pcm-00000000000000000023.aqp" &&
                    written.message_identity.byte_size > chunk.bytes.size() &&
                    written.document_identity.byte_size == std::filesystem::file_size(written.path),
                "written_pcm_block_metadata_was_incomplete");
        require_same(chunk, written.chunk);

        const auto original = read_bytes(written.path);
        const auto reopened_result = qa::read_pcm_block(written.path);
        const auto &reopened = stored(reopened_result, "pcm_block_was_not_reopened");
        require(reopened.sequence == written.sequence &&
                    reopened.message_identity == written.message_identity &&
                    reopened.document_identity == written.document_identity,
                "reopened_pcm_block_lost_its_own_identity");
        require_same(chunk, reopened.chunk);

        expect_error(qa::write_pcm_block(directory(reserved), 23, chunk),
                     qa::PcmBlockStoreError::already_exists, "pcm_block_was_replaced");
        require(read_bytes(written.path) == original, "pcm_block_collision_changed_original_bytes");

        auto wrong_size = original;
        wrong_size[28] ^= std::byte{1};
        const auto wrong_size_path = written.path.parent_path() / "wrong-size.aqp";
        write_bytes(wrong_size_path, wrong_size);
        expect_error(qa::read_pcm_block(wrong_size_path),
                     qa::PcmBlockStoreError::pcm_length_mismatch, "modified_pcm_size_was_accepted");

        auto modified = original;
        modified.back() ^= std::byte{1};
        const auto modified_message =
            std::span<const std::byte>{modified}.subspan(qa::pcm_block_header_bytes);
        const auto modified_message_identity = qa::identify_content(modified_message);
        for (std::size_t index{}; index < modified_message_identity.sha256.size(); ++index) {
            modified[32 + index] = static_cast<std::byte>(modified_message_identity.sha256[index]);
        }
        const auto modified_path = written.path.parent_path() / "modified.aqp";
        write_bytes(modified_path, modified);
        expect_error(qa::read_pcm_block(modified_path), qa::PcmBlockStoreError::hash_mismatch,
                     "modified_pcm_byte_was_not_detected_independently");

        remove_tree(fixture);
        std::puts("pcm_block_store_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "pcm_block_store_test: %s\n", error.what());
        return 1;
    }
}
