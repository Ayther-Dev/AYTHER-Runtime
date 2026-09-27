#include "file_reservation.h"

#include <fstream>
#include <iostream>

int main() {
  using ayther::runtime::FileReservation;
  FileReservation empty("", "");
  bool missing_rejected = false;
  try {
    FileReservation missing("", "requested-revision");
  } catch (const std::exception &) {
    missing_rejected = true;
  }
  if (!missing_rejected)
    return 1;
#ifdef _WIN32
  const std::string path = "reservation-fixture.bin";
  {
    std::ofstream file(path);
    file << "fixture";
  }
  {
    FileReservation reserved(path, "");
    const auto writer =
        CreateFileA(path.c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (writer != INVALID_HANDLE_VALUE) {
      CloseHandle(writer);
      return 2;
    }
    if (DeleteFileA(path.c_str()))
      return 3;
    if (MoveFileA(path.c_str(), "reservation-renamed.bin"))
      return 4;
  }
  bool mismatch_rejected = false;
  try {
    FileReservation mismatch(path, "wrong-revision");
  } catch (const std::exception &) {
    mismatch_rejected = true;
  }
  if (!mismatch_rejected)
    return 5;
  const auto writer =
      CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (writer == INVALID_HANDLE_VALUE)
    return 6;
  CloseHandle(writer);
  if (!DeleteFileA(path.c_str()))
    return 7;
#else
  bool unsupported_rejected = false;
  try {
    FileReservation unsupported("fixture.bin", "requested-revision");
  } catch (const std::exception &) {
    unsupported_rejected = true;
  }
  if (!unsupported_rejected)
    return 8;
#endif
  std::cout << "Reservation lifetime and rejection contract passed.\n";
}
