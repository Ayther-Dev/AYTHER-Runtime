#include "exclusive_evidence_directory.h"
#include "pcm_block_store.h"
#include "pcm_message.h"
#include "wav_derivation.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

const qa::ExclusiveEvidenceDirectory &
directory(const qa::ExclusiveEvidenceDirectoryResult &result) {
    const auto *value = std::get_if<qa::ExclusiveEvidenceDirectory>(&result);
    require(value != nullptr, "evidence_directory_reservation_failed");
    return *value;
}

qa::AudioChunk chunk(const std::uint64_t sequence, const std::uint64_t begin,
                     const std::uint64_t end, const std::vector<std::byte> &bytes) {
    qa::AudioChunk value;
    value.run_id = "run-147";
    value.capture_point = "session-postmix";
    value.producer_sequence = sequence;
    value.format = {qa::PcmFormat::s16le, 44'100, 2};
    value.range = {"captured-output", 44'100, begin, end};
    value.bytes = bytes;
    value.sha256 = {qa::Availability::known, qa::pcm_sha256(value.bytes), {}};
    value.durability = qa::Durability::pending;
    value.checkpoint_id = {qa::Availability::not_applicable, std::nullopt, "not_published"};
    return value;
}

std::filesystem::path write_block(const qa::ExclusiveEvidenceDirectory &run_directory,
                                  const std::uint64_t sequence, const qa::AudioChunk &value) {
    const auto written = qa::write_pcm_block(run_directory, sequence, value);
    const auto *block = std::get_if<qa::StoredPcmBlock>(&written);
    require(block != nullptr, "wav_pcm_block_fixture_failed");
    return block->path;
}

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "wav_fixture_open_failed");
    const auto size = input.tellg();
    require(size >= 0, "wav_fixture_size_failed");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    require(static_cast<bool>(input.read(reinterpret_cast<char *>(bytes.data()), size)),
            "wav_fixture_read_failed");
    return bytes;
}

std::uint32_t u32(const std::vector<std::byte> &bytes, const std::size_t offset) {
    std::uint32_t value{};
    for (std::size_t index{}; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-147-wav";
    remove_tree(fixture);
    try {
        const auto reserved = qa::create_exclusive_evidence_directory(fixture, "run-147");
        const auto &run_directory = directory(reserved);
        const std::vector<std::byte> first_pcm{std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                                               std::byte{0x04}, std::byte{0x05}, std::byte{0x06},
                                               std::byte{0x07}, std::byte{0x08}};
        const std::vector<std::byte> second_pcm{std::byte{0x11}, std::byte{0x12}, std::byte{0x13},
                                                std::byte{0x14}, std::byte{0x15}, std::byte{0x16},
                                                std::byte{0x17}, std::byte{0x18}};
        const auto first = write_block(run_directory, 1, chunk(1, 0, 2, first_pcm));
        const auto second = write_block(run_directory, 2, chunk(2, 2, 4, second_pcm));
        const auto wav_path = run_directory.path() / "derived.wav";
        const auto derived = qa::derive_wav({first, second}, wav_path);
        const auto *wav = std::get_if<qa::DerivedWav>(&derived);
        require(wav != nullptr && wav->range.begin == 0 && wav->range.end == 4 &&
                    wav->block_count == 2 &&
                    wav->pcm_identity.byte_size == first_pcm.size() + second_pcm.size(),
                "contiguous_pcm_blocks_were_not_derived");
        const auto wav_bytes = read_bytes(wav_path);
        std::vector<std::byte> expected_pcm = first_pcm;
        expected_pcm.insert(expected_pcm.end(), second_pcm.begin(), second_pcm.end());
        require(wav_bytes.size() == 44 + expected_pcm.size() &&
                    u32(wav_bytes, 40) == expected_pcm.size() &&
                    std::vector<std::byte>(wav_bytes.begin() + 44, wav_bytes.end()) == expected_pcm,
                "wav_samples_differed_from_verified_blocks");

        const auto gap = write_block(run_directory, 3, chunk(3, 5, 7, second_pcm));
        const auto gap_path = run_directory.path() / "gap.wav";
        const auto rejected = qa::derive_wav({first, gap}, gap_path);
        require(std::get_if<qa::WavDerivationError>(&rejected) != nullptr &&
                    std::get<qa::WavDerivationError>(rejected) ==
                        qa::WavDerivationError::non_contiguous &&
                    !std::filesystem::exists(gap_path),
                "wav_gap_was_filled_or_published");

        remove_tree(fixture);
        std::puts("wav_derivation_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "wav_derivation_test: %s\n", error.what());
        return 1;
    }
}
