#include "save_state_store.h"

#include <cctype>
#include <ctime>
#include <fstream>
#include <limits>
#include <system_error>

namespace ayther::runtime {
namespace {

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
                {}, RuntimeErrorCode::state_restore_failed,
                filesystem_error ? filesystem_error.message()
                                 : "state file does not exist"};
    }
    const std::uintmax_t size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error || size == 0 ||
        size > static_cast<std::uintmax_t>(SIZE_MAX) ||
        size > static_cast<std::uintmax_t>(
                   std::numeric_limits<std::streamsize>::max())) {
        return {SaveStateStatus::invalid, {},
                RuntimeErrorCode::state_restore_failed,
                filesystem_error ? filesystem_error.message()
                                 : "state file has an invalid size"};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size())) ||
        input.peek() != std::ifstream::traits_type::eof()) {
        return {SaveStateStatus::io_error, {},
                RuntimeErrorCode::state_restore_failed,
                "could not read the complete state file"};
    }
    return {SaveStateStatus::loaded, std::move(bytes),
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

    bool written = false;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (output) {
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            output.flush();
            written = static_cast<bool>(output);
            output.close();
            written = written && !output.fail();
        }
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
