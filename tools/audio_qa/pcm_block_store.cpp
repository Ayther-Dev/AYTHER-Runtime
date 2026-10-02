#include "pcm_block_store.h"

#include "content_hash.h"
#include "exclusive_file.h"
#include "pcm_message.h"
#include "protocol_header.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <span>
#include <sstream>
#include <system_error>
#include <vector>

namespace ayther::audio_qa {
namespace {

constexpr std::array<std::byte, 8> block_magic{std::byte{'A'}, std::byte{'Y'}, std::byte{'T'},
                                               std::byte{'P'}, std::byte{'C'}, std::byte{'M'},
                                               std::byte{'0'}, std::byte{'1'}};
constexpr std::uint16_t block_version_major = 1;
constexpr std::uint16_t block_version_minor = 0;
constexpr std::size_t sequence_offset = 16;
constexpr std::size_t message_size_offset = 24;
constexpr std::size_t pcm_size_offset = 28;
constexpr std::size_t message_hash_offset = 32;
constexpr std::size_t max_pcm_block_bytes =
    pcm_block_header_bytes + protocol_header_bytes + max_protocol_payload_bytes;

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

std::filesystem::path block_path(const ExclusiveEvidenceDirectory &directory,
                                 const std::uint64_t sequence) {
    std::ostringstream name;
    name << "pcm-" << std::setfill('0') << std::setw(20) << sequence << ".aqp";
    return directory.path() / "audio" / name.str();
}

bool ensure_audio_directory(const std::filesystem::path &path) noexcept {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (!error && std::filesystem::exists(status)) {
        return std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status);
    }
    error.clear();
    return std::filesystem::create_directory(path, error) && !error;
}

PcmBlockStoreError map_message_error(const PcmMessageError error) noexcept {
    switch (error) {
    case PcmMessageError::sequence_mismatch:
        return PcmBlockStoreError::sequence_mismatch;
    case PcmMessageError::hash_mismatch:
        return PcmBlockStoreError::hash_mismatch;
    case PcmMessageError::pcm_length_mismatch:
        return PcmBlockStoreError::pcm_length_mismatch;
    case PcmMessageError::message_too_large:
    case PcmMessageError::metadata_too_large:
        return PcmBlockStoreError::document_too_large;
    case PcmMessageError::invalid_chunk:
    case PcmMessageError::header_rejected:
    case PcmMessageError::wrong_message_type:
    case PcmMessageError::truncated:
    case PcmMessageError::length_mismatch:
    case PcmMessageError::malformed_metadata:
    case PcmMessageError::hash_required:
        return PcmBlockStoreError::invalid_content;
    }
    return PcmBlockStoreError::invalid_content;
}

} // namespace

PcmBlockStoreResult write_pcm_block(const ExclusiveEvidenceDirectory &directory,
                                    const std::uint64_t sequence,
                                    const AudioChunk &chunk) noexcept {
    try {
        if (sequence == 0) {
            return PcmBlockStoreError::invalid_sequence;
        }
        const auto encoded = encode_pcm_message(chunk, sequence);
        const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
        if (message == nullptr) {
            const auto error = std::get<PcmMessageError>(encoded);
            if (error == PcmMessageError::message_too_large ||
                error == PcmMessageError::metadata_too_large) {
                return PcmBlockStoreError::document_too_large;
            }
            return PcmBlockStoreError::invalid_chunk;
        }
        if (message->size() > max_pcm_block_bytes - pcm_block_header_bytes ||
            chunk.bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            return PcmBlockStoreError::document_too_large;
        }
        const auto message_identity = identify_content(*message);
        std::vector<std::byte> document(pcm_block_header_bytes + message->size());
        std::copy(block_magic.begin(), block_magic.end(), document.begin());
        write_le<std::uint16_t>(document, 8, block_version_major);
        write_le<std::uint16_t>(document, 10, block_version_minor);
        write_le<std::uint32_t>(document, 12, static_cast<std::uint32_t>(pcm_block_header_bytes));
        write_le(document, sequence_offset, sequence);
        write_le(document, message_size_offset, static_cast<std::uint32_t>(message->size()));
        write_le(document, pcm_size_offset, static_cast<std::uint32_t>(chunk.bytes.size()));
        for (std::size_t index{}; index < message_identity.sha256.size(); ++index) {
            document[message_hash_offset + index] =
                static_cast<std::byte>(message_identity.sha256[index]);
        }
        std::copy(message->begin(), message->end(), document.begin() + pcm_block_header_bytes);

        const auto path = block_path(directory, sequence);
        if (!ensure_audio_directory(path.parent_path())) {
            return PcmBlockStoreError::io_error;
        }
        switch (write_exclusive_file(path, document)) {
        case ExclusiveFileWriteResult::already_exists:
            return PcmBlockStoreError::already_exists;
        case ExclusiveFileWriteResult::io_error:
            return PcmBlockStoreError::io_error;
        case ExclusiveFileWriteResult::written:
            return StoredPcmBlock{path, sequence, message_identity, identify_content(document),
                                  chunk};
        }
    } catch (...) {
        return PcmBlockStoreError::io_error;
    }
    return PcmBlockStoreError::io_error;
}

PcmBlockStoreResult read_pcm_block(const std::filesystem::path &path) noexcept {
    try {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) {
            return PcmBlockStoreError::io_error;
        }
        if (size > max_pcm_block_bytes) {
            return PcmBlockStoreError::document_too_large;
        }
        if (size < pcm_block_header_bytes) {
            return PcmBlockStoreError::malformed_header;
        }
        std::vector<std::byte> document(static_cast<std::size_t>(size));
        std::ifstream input(path, std::ios::binary);
        if (!input ||
            !input.read(reinterpret_cast<char *>(document.data()),
                        static_cast<std::streamsize>(document.size())) ||
            input.peek() != std::char_traits<char>::eof()) {
            return PcmBlockStoreError::io_error;
        }
        const std::span<const std::byte> bytes{document};
        if (!std::equal(block_magic.begin(), block_magic.end(), bytes.begin())) {
            return PcmBlockStoreError::malformed_header;
        }
        if (read_le<std::uint16_t>(bytes, 8) != block_version_major ||
            read_le<std::uint16_t>(bytes, 10) != block_version_minor) {
            return PcmBlockStoreError::incompatible_version;
        }
        if (read_le<std::uint32_t>(bytes, 12) != pcm_block_header_bytes) {
            return PcmBlockStoreError::malformed_header;
        }
        const auto sequence = read_le<std::uint64_t>(bytes, sequence_offset);
        const auto message_size = read_le<std::uint32_t>(bytes, message_size_offset);
        const auto pcm_size = read_le<std::uint32_t>(bytes, pcm_size_offset);
        if (sequence == 0) {
            return PcmBlockStoreError::invalid_sequence;
        }
        if (message_size != bytes.size() - pcm_block_header_bytes) {
            return PcmBlockStoreError::length_mismatch;
        }
        const auto message = bytes.subspan(pcm_block_header_bytes);
        const auto message_identity = identify_content(message);
        for (std::size_t index{}; index < message_identity.sha256.size(); ++index) {
            if (std::to_integer<std::uint8_t>(bytes[message_hash_offset + index]) !=
                message_identity.sha256[index]) {
                return PcmBlockStoreError::hash_mismatch;
            }
        }
        const auto decoded = decode_pcm_message(message, sequence);
        const auto *chunk = std::get_if<AudioChunk>(&decoded);
        if (chunk == nullptr) {
            return map_message_error(std::get<PcmMessageError>(decoded));
        }
        if (chunk->bytes.size() != pcm_size) {
            return PcmBlockStoreError::pcm_length_mismatch;
        }
        return StoredPcmBlock{path, sequence, message_identity, identify_content(document), *chunk};
    } catch (...) {
        return PcmBlockStoreError::io_error;
    }
}

} // namespace ayther::audio_qa
