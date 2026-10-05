#include "fact_fragment_store.h"

#include "content_hash.h"
#include "exclusive_file.h"
#include "fact_batch.h"
#include "long_path.h"
#include "protocol_header.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace ayther::audio_qa {
namespace {

constexpr std::array<std::byte, 8> fragment_magic{std::byte{'A'}, std::byte{'Y'}, std::byte{'T'},
                                                  std::byte{'F'}, std::byte{'A'}, std::byte{'C'},
                                                  std::byte{'T'}, std::byte{'1'}};
constexpr std::uint16_t fragment_version_major = 1;
constexpr std::uint16_t fragment_version_minor = 0;
constexpr std::size_t sequence_offset = 16;
constexpr std::size_t record_count_offset = 24;
constexpr std::size_t payload_size_offset = 28;
constexpr std::size_t payload_hash_offset = 32;
constexpr std::size_t max_fact_fragment_bytes =
    fact_fragment_header_bytes + protocol_header_bytes + max_protocol_payload_bytes;

template <typename UInt>
void write_le(std::span<std::byte> bytes, const std::size_t offset, const UInt value) noexcept {
    for (std::size_t index{}; index < sizeof(UInt); ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

template <typename UInt>
UInt read_le(const std::span<const std::byte> bytes, const std::size_t offset) noexcept {
    UInt value{};
    for (std::size_t index{}; index < sizeof(UInt); ++index) {
        value |= static_cast<UInt>(std::to_integer<unsigned>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

std::filesystem::path fragment_path(const ExclusiveEvidenceDirectory &directory,
                                    const std::uint64_t sequence) {
    std::ostringstream name;
    name << "facts-" << std::setfill('0') << std::setw(20) << sequence << ".aqf";
    return directory.path() / "fragments" / name.str();
}

bool ensure_fragment_directory(const std::filesystem::path &directory) noexcept {
    // D-12: through the extended form of the path, which has no MAX_PATH limit.
    const auto path = long_path(directory);
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (!error && std::filesystem::exists(status)) {
        return std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status);
    }
    error.clear();
    return std::filesystem::create_directory(path, error) && !error;
}

FactFragmentStoreError map_batch_error(const FactBatchError error) noexcept {
    switch (error) {
    case FactBatchError::sequence_mismatch:
        return FactFragmentStoreError::sequence_mismatch;
    case FactBatchError::invalid_count:
        return FactFragmentStoreError::count_mismatch;
    case FactBatchError::batch_too_large:
        return FactFragmentStoreError::document_too_large;
    case FactBatchError::invalid_fact:
    case FactBatchError::fact_too_large:
    case FactBatchError::header_rejected:
    case FactBatchError::wrong_message_type:
    case FactBatchError::truncated:
    case FactBatchError::length_mismatch:
    case FactBatchError::malformed_fact:
        return FactFragmentStoreError::invalid_content;
    }
    return FactFragmentStoreError::invalid_content;
}

} // namespace

FactFragmentStoreResult write_fact_fragment(const ExclusiveEvidenceDirectory &directory,
                                            const std::uint64_t sequence,
                                            const std::vector<Fact> &facts) noexcept {
    try {
        if (sequence == 0) {
            return FactFragmentStoreError::invalid_sequence;
        }
        const auto encoded = encode_fact_batch(facts, sequence);
        const auto *payload = std::get_if<std::vector<std::byte>>(&encoded);
        if (payload == nullptr) {
            const auto error = std::get<FactBatchError>(encoded);
            return error == FactBatchError::batch_too_large
                       ? FactFragmentStoreError::document_too_large
                       : FactFragmentStoreError::invalid_facts;
        }
        if (payload->size() > max_fact_fragment_bytes - fact_fragment_header_bytes) {
            return FactFragmentStoreError::document_too_large;
        }
        const auto payload_identity = identify_content(*payload);
        std::vector<std::byte> document(fact_fragment_header_bytes + payload->size());
        std::copy(fragment_magic.begin(), fragment_magic.end(), document.begin());
        write_le<std::uint16_t>(document, 8, fragment_version_major);
        write_le<std::uint16_t>(document, 10, fragment_version_minor);
        write_le<std::uint32_t>(document, 12,
                                static_cast<std::uint32_t>(fact_fragment_header_bytes));
        write_le(document, sequence_offset, sequence);
        write_le(document, record_count_offset, static_cast<std::uint32_t>(facts.size()));
        write_le(document, payload_size_offset, static_cast<std::uint32_t>(payload->size()));
        for (std::size_t index{}; index < payload_identity.sha256.size(); ++index) {
            document[payload_hash_offset + index] =
                static_cast<std::byte>(payload_identity.sha256[index]);
        }
        std::copy(payload->begin(), payload->end(), document.begin() + fact_fragment_header_bytes);

        const auto path = fragment_path(directory, sequence);
        if (!ensure_fragment_directory(path.parent_path())) {
            return FactFragmentStoreError::io_error;
        }
        switch (write_exclusive_file(path, document)) {
        case ExclusiveFileWriteResult::already_exists:
            return FactFragmentStoreError::already_exists;
        case ExclusiveFileWriteResult::io_error:
            return FactFragmentStoreError::io_error;
        case ExclusiveFileWriteResult::written:
            return StoredFactFragment{
                path,
                sequence,
                static_cast<std::uint32_t>(facts.size()),
                payload_identity,
                identify_content(document),
                facts,
            };
        }
    } catch (...) {
        return FactFragmentStoreError::io_error;
    }
    return FactFragmentStoreError::io_error;
}

FactFragmentStoreResult read_fact_fragment(const std::filesystem::path &path) noexcept {
    try {
        std::error_code error;
        // D-12: read through the extended form of the path.
        const auto native = long_path(path);
        const auto size = std::filesystem::file_size(native, error);
        if (error) {
            return FactFragmentStoreError::io_error;
        }
        if (size > max_fact_fragment_bytes) {
            return FactFragmentStoreError::document_too_large;
        }
        if (size < fact_fragment_header_bytes) {
            return FactFragmentStoreError::malformed_header;
        }
        std::vector<std::byte> document(static_cast<std::size_t>(size));
        std::ifstream input(native, std::ios::binary);
        if (!input ||
            !input.read(reinterpret_cast<char *>(document.data()),
                        static_cast<std::streamsize>(document.size())) ||
            input.peek() != std::char_traits<char>::eof()) {
            return FactFragmentStoreError::io_error;
        }
        const std::span<const std::byte> bytes{document};
        if (!std::equal(fragment_magic.begin(), fragment_magic.end(), bytes.begin())) {
            return FactFragmentStoreError::malformed_header;
        }
        if (read_le<std::uint16_t>(bytes, 8) != fragment_version_major ||
            read_le<std::uint16_t>(bytes, 10) != fragment_version_minor) {
            return FactFragmentStoreError::incompatible_version;
        }
        if (read_le<std::uint32_t>(bytes, 12) != fact_fragment_header_bytes) {
            return FactFragmentStoreError::malformed_header;
        }
        const auto sequence = read_le<std::uint64_t>(bytes, sequence_offset);
        const auto record_count = read_le<std::uint32_t>(bytes, record_count_offset);
        const auto payload_size = read_le<std::uint32_t>(bytes, payload_size_offset);
        if (sequence == 0) {
            return FactFragmentStoreError::invalid_sequence;
        }
        if (record_count == 0 || record_count > max_fact_batch_records) {
            return FactFragmentStoreError::count_mismatch;
        }
        if (payload_size != bytes.size() - fact_fragment_header_bytes) {
            return FactFragmentStoreError::length_mismatch;
        }
        const auto payload = bytes.subspan(fact_fragment_header_bytes);
        const auto payload_identity = identify_content(payload);
        for (std::size_t index{}; index < payload_identity.sha256.size(); ++index) {
            if (std::to_integer<std::uint8_t>(bytes[payload_hash_offset + index]) !=
                payload_identity.sha256[index]) {
                return FactFragmentStoreError::hash_mismatch;
            }
        }
        const auto decoded = decode_fact_batch(payload, sequence);
        const auto *facts = std::get_if<std::vector<Fact>>(&decoded);
        if (facts == nullptr) {
            return map_batch_error(std::get<FactBatchError>(decoded));
        }
        if (facts->size() != record_count) {
            return FactFragmentStoreError::count_mismatch;
        }
        return StoredFactFragment{
            path, sequence, record_count, payload_identity, identify_content(document), *facts};
    } catch (...) {
        return FactFragmentStoreError::io_error;
    }
}

} // namespace ayther::audio_qa
