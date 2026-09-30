#include "inherited_channel.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ayther::audio_qa {
namespace {

bool valid_handle(const NativeChannelHandle handle) noexcept {
#ifdef _WIN32
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
#else
    return handle >= 0;
#endif
}

void close_handle(const NativeChannelHandle handle) noexcept {
    if (!valid_handle(handle))
        return;
#ifdef _WIN32
    (void)CloseHandle(handle);
#else
    (void)close(handle);
#endif
}

} // namespace

OwnedChannelHandle::~OwnedChannelHandle() { reset(); }

OwnedChannelHandle::OwnedChannelHandle(OwnedChannelHandle &&other) noexcept
    : handle_(other.release()) {}

OwnedChannelHandle &OwnedChannelHandle::operator=(OwnedChannelHandle &&other) noexcept {
    if (this != &other)
        reset(other.release());
    return *this;
}

OwnedChannelHandle::operator bool() const noexcept { return valid_handle(handle_); }

NativeChannelHandle OwnedChannelHandle::release() noexcept {
    const auto result = handle_;
    handle_ = invalid_channel_handle;
    return result;
}

void OwnedChannelHandle::reset(const NativeChannelHandle replacement) noexcept {
    if (handle_ != replacement)
        close_handle(handle_);
    handle_ = replacement;
}

ChannelCreateResult create_inherited_data_channel() noexcept {
#ifdef _WIN32
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE read_handle = nullptr;
    HANDLE write_handle = nullptr;
    if (!CreatePipe(&read_handle, &write_handle, &attributes, 0))
        return ChannelError::create_failed;
    OwnedChannelHandle read{read_handle};
    OwnedChannelHandle write{write_handle};
    if (!SetHandleInformation(read.get(), HANDLE_FLAG_INHERIT, 0))
        return ChannelError::configure_failed;
    return InheritedDataChannel{std::move(read), std::move(write)};
#else
    int handles[2]{};
    if (pipe(handles) != 0)
        return ChannelError::create_failed;
    OwnedChannelHandle read{handles[0]};
    OwnedChannelHandle write{handles[1]};
    const int read_flags = fcntl(read.get(), F_GETFD);
    const int write_flags = fcntl(write.get(), F_GETFD);
    if (read_flags < 0 || write_flags < 0 ||
        fcntl(read.get(), F_SETFD, read_flags | FD_CLOEXEC) != 0 ||
        fcntl(write.get(), F_SETFD, write_flags & ~FD_CLOEXEC) != 0)
        return ChannelError::configure_failed;
    return InheritedDataChannel{std::move(read), std::move(write)};
#endif
}

ControlChannelCreateResult create_inherited_control_channel() noexcept {
#ifdef _WIN32
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE read_handle = nullptr;
    HANDLE write_handle = nullptr;
    if (!CreatePipe(&read_handle, &write_handle, &attributes, 0))
        return ChannelError::create_failed;
    OwnedChannelHandle read{read_handle};
    OwnedChannelHandle write{write_handle};
    if (!SetHandleInformation(write.get(), HANDLE_FLAG_INHERIT, 0))
        return ChannelError::configure_failed;
    return InheritedControlChannel{std::move(write), std::move(read)};
#else
    int handles[2]{};
    if (pipe(handles) != 0)
        return ChannelError::create_failed;
    OwnedChannelHandle read{handles[0]};
    OwnedChannelHandle write{handles[1]};
    const int read_flags = fcntl(read.get(), F_GETFD);
    const int write_flags = fcntl(write.get(), F_GETFD);
    if (read_flags < 0 || write_flags < 0 ||
        fcntl(read.get(), F_SETFD, read_flags & ~FD_CLOEXEC) != 0 ||
        fcntl(write.get(), F_SETFD, write_flags | FD_CLOEXEC) != 0)
        return ChannelError::configure_failed;
    return InheritedControlChannel{std::move(write), std::move(read)};
#endif
}

ChannelAdoptResult adopt_inherited_data_channel(const std::string_view token) noexcept {
    if (token.empty() || token.front() == '+' || token.front() == '-')
        return ChannelError::invalid_token;
    std::uintptr_t value{};
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || value == 0)
        return ChannelError::invalid_token;
#ifdef _WIN32
    const auto handle = reinterpret_cast<HANDLE>(value);
    DWORD flags{};
    if (!GetHandleInformation(handle, &flags) || GetFileType(handle) != FILE_TYPE_PIPE)
        return ChannelError::invalid_handle;
    return OwnedChannelHandle{handle};
#else
    if (value > static_cast<std::uintptr_t>(std::numeric_limits<int>::max()))
        return ChannelError::invalid_token;
    const auto handle = static_cast<int>(value);
    if (fcntl(handle, F_GETFD) < 0)
        return ChannelError::invalid_handle;
    return OwnedChannelHandle{handle};
#endif
}

std::string inherited_data_channel_token(const NativeChannelHandle handle) {
#ifdef _WIN32
    return std::to_string(reinterpret_cast<std::uintptr_t>(handle));
#else
    return std::to_string(handle);
#endif
}

bool channel_handle_is_inheritable(const NativeChannelHandle handle) noexcept {
    if (!valid_handle(handle))
        return false;
#ifdef _WIN32
    DWORD flags{};
    return GetHandleInformation(handle, &flags) && (flags & HANDLE_FLAG_INHERIT) != 0;
#else
    const int flags = fcntl(handle, F_GETFD);
    return flags >= 0 && (flags & FD_CLOEXEC) == 0;
#endif
}

bool write_channel(OwnedChannelHandle &channel, const std::span<const std::byte> bytes) noexcept {
    if (!channel || bytes.empty())
        return false;
    std::size_t cursor = 0;
    while (cursor < bytes.size()) {
#ifdef _WIN32
        const auto remaining = bytes.size() - cursor;
        const auto request = static_cast<DWORD>(
            (std::min)(remaining, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written{};
        if (!WriteFile(channel.get(), bytes.data() + cursor, request, &written, nullptr) ||
            written == 0)
            return false;
        cursor += written;
#else
        const auto written = write(channel.get(), bytes.data() + cursor, bytes.size() - cursor);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return false;
        cursor += static_cast<std::size_t>(written);
#endif
    }
    return true;
}

ChannelReadResult read_channel(OwnedChannelHandle &channel, const std::span<std::byte> buffer,
                               std::size_t &bytes_read) noexcept {
    bytes_read = 0;
    if (!channel || buffer.empty())
        return ChannelReadResult::failed;
#ifdef _WIN32
    const auto request = static_cast<DWORD>(
        (std::min)(buffer.size(), static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
    DWORD received{};
    if (!ReadFile(channel.get(), buffer.data(), request, &received, nullptr)) {
        return GetLastError() == ERROR_BROKEN_PIPE ? ChannelReadResult::end_of_stream
                                                   : ChannelReadResult::failed;
    }
    bytes_read = received;
    return received == 0 ? ChannelReadResult::end_of_stream : ChannelReadResult::data;
#else
    while (true) {
        const auto received = read(channel.get(), buffer.data(), buffer.size());
        if (received < 0 && errno == EINTR)
            continue;
        if (received < 0)
            return ChannelReadResult::failed;
        bytes_read = static_cast<std::size_t>(received);
        return received == 0 ? ChannelReadResult::end_of_stream : ChannelReadResult::data;
    }
#endif
}

} // namespace ayther::audio_qa
