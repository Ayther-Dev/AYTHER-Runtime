#include "exclusive_file.h"

#include <cstddef>
#include <filesystem>
#include <span>
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

ExclusiveFileWriteResult write_exclusive_file(const std::filesystem::path &path,
                                              const std::span<const std::byte> bytes) noexcept {
#ifdef _WIN32
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS
                   ? ExclusiveFileWriteResult::already_exists
                   : ExclusiveFileWriteResult::io_error;
    }
    std::size_t offset{};
    bool written = true;
    while (offset < bytes.size()) {
        DWORD count{};
        const auto remaining = static_cast<DWORD>(bytes.size() - offset);
        if (!WriteFile(handle, bytes.data() + offset, remaining, &count, nullptr) || count == 0) {
            written = false;
            break;
        }
        offset += count;
    }
    if (!CloseHandle(handle)) {
        written = false;
    }
#else
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0) {
        return errno == EEXIST ? ExclusiveFileWriteResult::already_exists
                               : ExclusiveFileWriteResult::io_error;
    }
    std::size_t offset{};
    bool written = true;
    while (offset < bytes.size()) {
        const auto count = ::write(descriptor, bytes.data() + offset, bytes.size() - offset);
        if (count <= 0) {
            written = false;
            break;
        }
        offset += static_cast<std::size_t>(count);
    }
    if (::close(descriptor) != 0) {
        written = false;
    }
#endif
    if (!written) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return ExclusiveFileWriteResult::io_error;
    }
    return ExclusiveFileWriteResult::written;
}

} // namespace ayther::audio_qa
