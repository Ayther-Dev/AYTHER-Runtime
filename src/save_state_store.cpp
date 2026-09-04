#include "save_state_store.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <ctime>
#include <fstream>
#include <limits>
#include <system_error>

namespace ayther::runtime {
namespace {

constexpr std::array<std::uint8_t, 8> save_state_magic{
    'A', 'Y', 'T', 'H', 'S', 'T', 'A', 'T'};
constexpr std::size_t save_state_header_size =
    save_state_magic.size() + sizeof(std::uint32_t) +
    sizeof(std::uint64_t) + sizeof(std::uint32_t);

[[nodiscard]] std::uint32_t checksum(
    const std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t value = 2166136261U;
    for (const std::uint8_t byte : bytes) {
        value ^= byte;
        value *= 16777619U;
    }
    return value;
}

template <typename Integer>
void append_little_endian(std::vector<std::uint8_t>& output, Integer value) {
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        output.push_back(static_cast<std::uint8_t>(value & 0xffU));
        value >>= 8U;
    }
}

template <typename Integer>
[[nodiscard]] Integer read_little_endian(
    const std::span<const std::uint8_t> bytes,
    const std::size_t offset) noexcept {
    Integer value{};
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        value |= static_cast<Integer>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::vector<std::uint8_t> encode_state(
    const std::span<const std::uint8_t> payload) {
    std::vector<std::uint8_t> encoded;
    encoded.reserve(save_state_header_size + payload.size());
    encoded.insert(encoded.end(), save_state_magic.begin(), save_state_magic.end());
    append_little_endian(encoded, save_state_format_version);
    append_little_endian(encoded, static_cast<std::uint64_t>(payload.size()));
    append_little_endian(encoded, checksum(payload));
    encoded.insert(encoded.end(), payload.begin(), payload.end());
    return encoded;
}

std::string current_timestamp() {
    char timestamp[32]{};
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    const bool has_utc = gmtime_s(&utc, &now) == 0;
#else
    const bool has_utc = gmtime_r(&now, &utc) != nullptr;
#endif
    if (!has_utc ||
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%dT%H%M%SZ", &utc) == 0) {
        return "unknown-time";
    }
    return timestamp;
}

}  // namespace

std::string SaveStateStore::sanitize_component(const std::string_view value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const char character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        sanitized += (std::isalnum(byte) != 0 || character == '-' ||
                      character == '_')
                         ? character
                         : '_';
    }
    return sanitized.empty() ? "sin_nombre" : sanitized;
}

SaveStateLoadResult SaveStateStore::load(
    const std::filesystem::path& path) const {
    std::error_code filesystem_error;
    if (!std::filesystem::exists(path, filesystem_error)) {
        return {filesystem_error ? SaveStateStatus::io_error
                                 : SaveStateStatus::missing,
                0U, {}, RuntimeErrorCode::state_restore_failed,
                filesystem_error ? filesystem_error.message()
                                 : "state file does not exist"};
    }
    const std::uintmax_t size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error || size == 0 ||
        size > static_cast<std::uintmax_t>(SIZE_MAX) ||
        size > static_cast<std::uintmax_t>(
                   std::numeric_limits<std::streamsize>::max())) {
        return {SaveStateStatus::invalid, 0U, {},
                RuntimeErrorCode::state_restore_failed,
                filesystem_error ? filesystem_error.message()
                                 : "state file has an invalid size"};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size())) ||
        input.peek() != std::ifstream::traits_type::eof()) {
        return {SaveStateStatus::io_error, 0U, {},
                RuntimeErrorCode::state_restore_failed,
                "could not read the complete state file"};
    }
    const bool has_magic = bytes.size() >= save_state_magic.size() &&
        std::equal(save_state_magic.begin(), save_state_magic.end(), bytes.begin());
    if (!has_magic) {
        return {SaveStateStatus::loaded, 0U, std::move(bytes),
                RuntimeErrorCode::state_restore_failed, {}};
    }
    if (bytes.size() < save_state_header_size) {
        return {SaveStateStatus::invalid, 0U, {},
                RuntimeErrorCode::state_restore_failed,
                "versioned state header is truncated"};
    }

    const std::span<const std::uint8_t> encoded{bytes};
    std::size_t offset = save_state_magic.size();
    const auto version = read_little_endian<std::uint32_t>(encoded, offset);
    offset += sizeof(std::uint32_t);
    if (version != save_state_format_version) {
        return {SaveStateStatus::unsupported_version, version, {},
                RuntimeErrorCode::state_restore_failed,
                "save-state format version is unsupported"};
    }
    const auto payload_size = read_little_endian<std::uint64_t>(encoded, offset);
    offset += sizeof(std::uint64_t);
    const auto expected_checksum =
        read_little_endian<std::uint32_t>(encoded, offset);
    offset += sizeof(std::uint32_t);
    if (payload_size != bytes.size() - offset) {
        return {SaveStateStatus::invalid, version, {},
                RuntimeErrorCode::state_restore_failed,
                "save-state payload length is invalid"};
    }
    const std::span<const std::uint8_t> payload{bytes.data() + offset,
                                                bytes.size() - offset};
    if (checksum(payload) != expected_checksum) {
        return {SaveStateStatus::invalid, version, {},
                RuntimeErrorCode::state_restore_failed,
                "save-state checksum mismatch"};
    }
    return {SaveStateStatus::loaded, version,
            std::vector<std::uint8_t>{payload.begin(), payload.end()},
            RuntimeErrorCode::state_restore_failed, {}};
}

SaveStateWriteResult SaveStateStore::save(
    const std::filesystem::path& root, const std::string_view game_id,
    const std::string_view profile, const std::string_view rom_revision,
    const std::span<const std::uint8_t> bytes,
    const std::string_view timestamp_utc) const {
    if (root.empty() || bytes.empty()) {
        return {SaveStateStatus::invalid, {},
                RuntimeErrorCode::state_save_failed,
                "save root and state bytes are required"};
    }
    if (fault_ == SaveStateWriteFault::disk_full) {
        return {SaveStateStatus::io_error, {},
                RuntimeErrorCode::persistence_io_failed,
                "simulated storage exhaustion"};
    }

    std::string revision = sanitize_component(
        rom_revision.empty() ? std::string_view{"sinrev"} : rom_revision);
    for (char& character : revision) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    const std::filesystem::path directory =
        root / sanitize_component(game_id.empty() ? std::string_view{"unknown"}
                                                   : game_id) /
        sanitize_component(profile.empty() ? std::string_view{"original"}
                                            : profile);
    std::error_code filesystem_error;
    std::filesystem::create_directories(directory, filesystem_error);
    if (filesystem_error) {
        return {SaveStateStatus::io_error, {},
                RuntimeErrorCode::persistence_io_failed,
                filesystem_error.message()};
    }

    const std::string timestamp = timestamp_utc.empty()
                                      ? current_timestamp()
                                      : sanitize_component(timestamp_utc);
    const std::filesystem::path destination =
        directory / ("estado-" + timestamp + "-" + revision + ".bin");
    const std::filesystem::path temporary = destination.string() + ".tmp";
    if (std::filesystem::exists(destination, filesystem_error)) {
        return {SaveStateStatus::target_exists, destination,
                RuntimeErrorCode::state_save_failed,
                "state destination already exists"};
    }

    const auto encoded = encode_state(bytes);
    bool written = false;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (output) {
            output.write(reinterpret_cast<const char*>(encoded.data()),
                         static_cast<std::streamsize>(encoded.size()));
            output.flush();
            written = static_cast<bool>(output);
            output.close();
            written = written && !output.fail();
        }
    }
    if (written && fault_ == SaveStateWriteFault::before_publish) {
        written = false;
        filesystem_error = std::make_error_code(std::errc::io_error);
    }
    if (written) {
        std::filesystem::rename(temporary, destination, filesystem_error);
        written = !filesystem_error;
    }
    if (!written) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        return {SaveStateStatus::io_error, destination,
                RuntimeErrorCode::persistence_io_failed,
                filesystem_error ? filesystem_error.message()
                                 : "could not write the complete state file"};
    }
    return {SaveStateStatus::saved, destination,
            RuntimeErrorCode::state_save_failed, {}};
}

}  // namespace ayther::runtime
