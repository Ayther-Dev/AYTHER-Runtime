#include "durable_file.h"

#include "content_hash.h"
#include "long_path.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ayther::audio_qa {

struct DurableFileFactory {
    static DurablePublishedFile create(std::filesystem::path path, const ContentIdentity identity) {
        return DurablePublishedFile(std::move(path), identity);
    }
};

namespace {

std::atomic<std::uint64_t> temporary_sequence{};
constexpr std::uint32_t max_temporary_attempts = 64;

std::filesystem::path temporary_path(const std::filesystem::path &parent,
                                     const std::uint64_t process, const std::uint64_t sequence) {
    return parent /
           (".ayther-durable-" + std::to_string(process) + "-" + std::to_string(sequence) + ".tmp");
}

bool valid_target(const std::filesystem::path &target) noexcept {
    if (target.empty() || target.filename().empty()) {
        return false;
    }
    std::error_code error;
    const auto parent = std::filesystem::symlink_status(target.parent_path(), error);
    if (error || !std::filesystem::is_directory(parent) || std::filesystem::is_symlink(parent)) {
        return false;
    }
    error.clear();
    const auto status = std::filesystem::symlink_status(target, error);
    if (error) {
        return error == std::errc::no_such_file_or_directory;
    }
    return !std::filesystem::exists(status) ||
           (std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status));
}

void remove_temporary(const std::filesystem::path &path) noexcept {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

#ifdef _WIN32

struct WindowsTemporary {
    std::filesystem::path path;
    HANDLE handle = INVALID_HANDLE_VALUE;
};

std::variant<WindowsTemporary, DurablePublishError>
create_temporary(const std::filesystem::path &parent) noexcept {
    const auto process = static_cast<std::uint64_t>(GetCurrentProcessId());
    for (std::uint32_t attempt{}; attempt < max_temporary_attempts; ++attempt) {
        auto path = temporary_path(parent, process,
                                   temporary_sequence.fetch_add(1, std::memory_order_relaxed) + 1);
        const auto handle =
            CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            return WindowsTemporary{std::move(path), handle};
        }
        const auto error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            return DurablePublishError::write_failed;
        }
    }
    return DurablePublishError::temporary_collision;
}

DurablePublishResult publish_windows(const std::filesystem::path &target,
                                     const std::span<const std::byte> bytes,
                                     const DurablePublicationLimits limits) {
    auto created = create_temporary(target.parent_path());
    auto *temporary = std::get_if<WindowsTemporary>(&created);
    if (temporary == nullptr) {
        return std::get<DurablePublishError>(created);
    }
    std::size_t offset{};
    while (offset < bytes.size()) {
        if (limits.writable_bytes && offset >= *limits.writable_bytes) {
            CloseHandle(temporary->handle);
            remove_temporary(temporary->path);
            return DurablePublishError::storage_exhausted;
        }
        auto remaining = (std::min)(bytes.size() - offset,
                                    static_cast<std::size_t>((std::numeric_limits<DWORD>::max)()));
        if (limits.writable_bytes) {
            remaining = (std::min)(remaining, *limits.writable_bytes - offset);
        }
        DWORD written{};
        if (!WriteFile(temporary->handle, bytes.data() + offset, static_cast<DWORD>(remaining),
                       &written, nullptr) ||
            written == 0) {
            CloseHandle(temporary->handle);
            remove_temporary(temporary->path);
            return DurablePublishError::write_failed;
        }
        offset += written;
    }
    if (limits.stop == DurablePublicationStop::before_flush) {
        CloseHandle(temporary->handle);
        remove_temporary(temporary->path);
        return DurablePublishError::interrupted_before_flush;
    }
    if (!FlushFileBuffers(temporary->handle)) {
        CloseHandle(temporary->handle);
        remove_temporary(temporary->path);
        return DurablePublishError::flush_failed;
    }
    if (!CloseHandle(temporary->handle)) {
        remove_temporary(temporary->path);
        return DurablePublishError::flush_failed;
    }
    if (limits.stop == DurablePublicationStop::after_flush) {
        remove_temporary(temporary->path);
        return DurablePublishError::interrupted_after_flush;
    }
    if (!MoveFileExW(temporary->path.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove_temporary(temporary->path);
        return DurablePublishError::publish_failed;
    }
    return DurableFileFactory::create(target, identify_content(bytes));
}

#else

struct PosixTemporary {
    std::filesystem::path path;
    int descriptor = -1;
};

std::variant<PosixTemporary, DurablePublishError>
create_temporary(const std::filesystem::path &parent) noexcept {
    const auto process = static_cast<std::uint64_t>(::getpid());
    for (std::uint32_t attempt{}; attempt < max_temporary_attempts; ++attempt) {
        auto path = temporary_path(parent, process,
                                   temporary_sequence.fetch_add(1, std::memory_order_relaxed) + 1);
        const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor >= 0) {
            return PosixTemporary{std::move(path), descriptor};
        }
        if (errno != EEXIST) {
            return DurablePublishError::write_failed;
        }
    }
    return DurablePublishError::temporary_collision;
}

DurablePublishResult publish_posix(const std::filesystem::path &target,
                                   const std::span<const std::byte> bytes,
                                   const DurablePublicationLimits limits) {
    auto created = create_temporary(target.parent_path());
    auto *temporary = std::get_if<PosixTemporary>(&created);
    if (temporary == nullptr) {
        return std::get<DurablePublishError>(created);
    }
    std::size_t offset{};
    while (offset < bytes.size()) {
        if (limits.writable_bytes && offset >= *limits.writable_bytes) {
            ::close(temporary->descriptor);
            remove_temporary(temporary->path);
            return DurablePublishError::storage_exhausted;
        }
        auto remaining = bytes.size() - offset;
        if (limits.writable_bytes) {
            remaining = (std::min)(remaining, *limits.writable_bytes - offset);
        }
        const auto written = ::write(temporary->descriptor, bytes.data() + offset, remaining);
        if (written <= 0) {
            ::close(temporary->descriptor);
            remove_temporary(temporary->path);
            return DurablePublishError::write_failed;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (limits.stop == DurablePublicationStop::before_flush) {
        ::close(temporary->descriptor);
        remove_temporary(temporary->path);
        return DurablePublishError::interrupted_before_flush;
    }
    if (::fsync(temporary->descriptor) != 0) {
        ::close(temporary->descriptor);
        remove_temporary(temporary->path);
        return DurablePublishError::flush_failed;
    }
    if (::close(temporary->descriptor) != 0) {
        remove_temporary(temporary->path);
        return DurablePublishError::flush_failed;
    }
    if (limits.stop == DurablePublicationStop::after_flush) {
        remove_temporary(temporary->path);
        return DurablePublishError::interrupted_after_flush;
    }
    if (::rename(temporary->path.c_str(), target.c_str()) != 0) {
        remove_temporary(temporary->path);
        return DurablePublishError::publish_failed;
    }
    const int parent = ::open(target.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    if (parent < 0 || ::fsync(parent) != 0) {
        if (parent >= 0) {
            ::close(parent);
        }
        return DurablePublishError::flush_failed;
    }
    if (::close(parent) != 0) {
        return DurablePublishError::flush_failed;
    }
    return DurableFileFactory::create(target, identify_content(bytes));
}

#endif

} // namespace

DurablePublishResult publish_durable_file(const std::filesystem::path &target,
                                          const std::span<const std::byte> bytes,
                                          const DurablePublicationLimits limits) noexcept {
    try {
        // D-12: the document may pass MAX_PATH; it is written and published through the
        // extended form of its path, and the caller keeps the path it gave.
        const auto native = long_path(target);
        if (!valid_target(native)) {
            return DurablePublishError::invalid_target;
        }
#ifdef _WIN32
        auto published = publish_windows(native, bytes, limits);
#else
        auto published = publish_posix(native, bytes, limits);
#endif
        if (const auto *file = std::get_if<DurablePublishedFile>(&published))
            return DurableFileFactory::create(target, file->identity());
        return published;
    } catch (...) {
        return DurablePublishError::write_failed;
    }
}

} // namespace ayther::audio_qa
