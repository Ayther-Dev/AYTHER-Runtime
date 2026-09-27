#pragma once
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace ayther::runtime {
// Owned by the session host, independently of the launcher's lifetime.
class FileReservation final {
public:
  FileReservation(const std::string &path, const std::string &expected) {
    if (path.empty()) {
      if (!expected.empty())
        throw std::runtime_error("Missing path for requested reservation");
      return;
    }
#ifdef _WIN32
    handle_ = CreateFileW(
        std::filesystem::path(std::u8string(path.begin(), path.end())).c_str(),
        GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE)
      throw std::runtime_error("File is busy, being modified, or unavailable");
    try {
      if (!expected.empty() && revision() != expected)
        throw std::runtime_error(
            "File revision changed since Play preparation");
    } catch (...) {
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
      throw;
    }
#else
    if (!expected.empty())
      throw std::runtime_error(
          "File reservations are unsupported on this platform");
#endif
  }
  ~FileReservation() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE)
      CloseHandle(handle_);
#endif
  }
  FileReservation(const FileReservation &) = delete;
  FileReservation &operator=(const FileReservation &) = delete;
  FileReservation(FileReservation &&) = delete;
  FileReservation &operator=(FileReservation &&) = delete;

private:
#ifdef _WIN32
  HANDLE handle_{INVALID_HANDLE_VALUE};
  [[nodiscard]] std::string revision() const {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle_, &info))
      throw std::runtime_error("Cannot verify file identity");
    const auto size =
        (std::uint64_t{info.nFileSizeHigh} << 32) | info.nFileSizeLow;
    const auto ticks =
        (std::uint64_t{info.ftLastWriteTime.dwHighDateTime} << 32) |
        info.ftLastWriteTime.dwLowDateTime;
    if (ticks < 116444736000000000ULL)
      throw std::runtime_error("File timestamp is unsupported");
    if (ticks - 116444736000000000ULL >
        std::numeric_limits<std::uint64_t>::max() / 100)
      throw std::runtime_error("File timestamp is outside the supported range");
    const auto modified = (ticks - 116444736000000000ULL) * 100;
    alignas(8) unsigned char buffer[1024]{};
    READ_FILE_USN_DATA request{2, 2};
    DWORD returned{};
    std::int64_t usn{};
    if (DeviceIoControl(handle_, FSCTL_READ_FILE_USN_DATA, &request,
                        sizeof(request), buffer, sizeof(buffer), &returned,
                        nullptr) &&
        returned >= 32) {
      std::uint16_t major{};
      std::int64_t observed_usn{};
      std::memcpy(&major, buffer + 4, sizeof(major));
      std::memcpy(&observed_usn, buffer + 24, sizeof(observed_usn));
      if (major == 2 && observed_usn > 0)
        usn = observed_usn;
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::hex << std::setfill('0') << std::setw(8)
        << info.dwVolumeSerialNumber << ':' << std::setw(8)
        << info.nFileIndexHigh << std::setw(8) << info.nFileIndexLow << ':'
        << std::setw(8) << info.ftCreationTime.dwHighDateTime << std::setw(8)
        << info.ftCreationTime.dwLowDateTime << '|' << std::dec << size << '|'
        << modified << '|' << usn;
    return out.str();
  }
#endif
};
} // namespace ayther::runtime
