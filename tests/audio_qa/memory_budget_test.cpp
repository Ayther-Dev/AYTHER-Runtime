#define NOMINMAX
#include <windows.h>

#include <psapi.h>

#include "observation_bridge.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

constexpr std::uint64_t mib = std::uint64_t{1} << 20U;
constexpr std::uint64_t coordinator_limit = 256U * mib;
constexpr std::uint64_t capture_increment_limit = 128U * mib;
constexpr std::uint64_t queue_limit = 64U * mib;
constexpr std::size_t coordinator_working_bytes = 192U * mib;
constexpr DWORD sample_interval_ms = 10U;
constexpr std::size_t measurement_pairs = 3U;

using ProductionBridge = qa::ProductionObservationBridge;

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void touch(std::vector<std::byte> &bytes) {
    constexpr std::size_t page = 4096U;
    std::uint8_t checksum{};
    for (std::size_t offset{}; offset < bytes.size(); offset += page) {
        bytes[offset] = static_cast<std::byte>((offset / page) & 0xffU);
        checksum ^= std::to_integer<std::uint8_t>(bytes[offset]);
    }
    if (!bytes.empty()) {
        bytes.back() = static_cast<std::byte>(checksum);
    }
}

int child(const std::string_view mode) {
    if (mode == "--baseline") {
        Sleep(400U);
        return 0;
    }
    if (mode == "--coordinator") {
        std::array<std::vector<std::byte>, 3> buffers{std::vector<std::byte>(32U * mib),
                                                      std::vector<std::byte>(152U * mib),
                                                      std::vector<std::byte>(8U * mib)};
        for (auto &buffer : buffers) {
            touch(buffer);
        }
        Sleep(400U);
        return buffers[0].empty() ? 2 : 0;
    }
    if (mode == "--capture") {
        auto bridge = std::make_unique<ProductionBridge>("run-188");
        if (!bridge->valid()) {
            return 3;
        }
        Sleep(400U);
        return bridge->losses() == qa::ObservationBridgeLosses{} ? 0 : 4;
    }
    return 5;
}

struct ProcessPeak {
    std::uint64_t private_bytes{};
    std::size_t samples{};
};

std::wstring executable_path() {
    std::wstring path(32768U, L'\0');
    const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    require(size != 0U && size < path.size(), "self_path_unavailable");
    path.resize(size);
    return path;
}

ProcessPeak measure_child(const std::wstring &executable, const std::wstring_view mode) {
    std::wstring command = L"\"" + executable + L"\" " + std::wstring{mode};
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    require(CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE,
            "measurement_child_launch_failed");

    ProcessPeak peak;
    bool measurement_failed{};
    for (;;) {
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(process.hProcess,
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                                 sizeof(counters)) == FALSE) {
            measurement_failed = true;
            break;
        }
        peak.private_bytes =
            (std::max)(peak.private_bytes, static_cast<std::uint64_t>(counters.PrivateUsage));
        ++peak.samples;
        const auto wait = WaitForSingleObject(process.hProcess, sample_interval_ms);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (wait != WAIT_TIMEOUT) {
            measurement_failed = true;
            break;
        }
    }
    DWORD exit_code{};
    const bool exited = WaitForSingleObject(process.hProcess, 5000U) == WAIT_OBJECT_0 &&
                        GetExitCodeProcess(process.hProcess, &exit_code) != FALSE;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    require(!measurement_failed && exited && exit_code == 0U && peak.samples >= 2U,
            "measurement_child_failed");
    return peak;
}

std::uint64_t delta(const ProcessPeak &enabled, const ProcessPeak &disabled) noexcept {
    return enabled.private_bytes > disabled.private_bytes
               ? enabled.private_bytes - disabled.private_bytes
               : 0U;
}

} // namespace

int wmain(const int argc, wchar_t **argv) {
    try {
        if (argc == 2) {
            const std::wstring_view mode{argv[1]};
            if (mode == L"--baseline") {
                return child("--baseline");
            }
            if (mode == L"--coordinator") {
                return child("--coordinator");
            }
            if (mode == L"--capture") {
                return child("--capture");
            }
            return 6;
        }

        static_assert(sizeof(ProductionBridge) <= queue_limit);
        static_assert(coordinator_working_bytes <= coordinator_limit);
        const auto executable = executable_path();
        std::uint64_t coordinator_peak_delta{};
        std::uint64_t capture_peak_delta{};
        for (std::size_t pair{}; pair < measurement_pairs; ++pair) {
            const auto coordinator_off = measure_child(executable, L"--baseline");
            const auto coordinator_on = measure_child(executable, L"--coordinator");
            const auto coordinator_delta = delta(coordinator_on, coordinator_off);
            require(coordinator_delta <= coordinator_limit,
                    "coordinator_private_memory_limit_exceeded");
            coordinator_peak_delta = (std::max)(coordinator_peak_delta, coordinator_delta);

            const auto capture_off = measure_child(executable, L"--baseline");
            const auto capture_on = measure_child(executable, L"--capture");
            const auto capture_delta = delta(capture_on, capture_off);
            require(capture_delta <= capture_increment_limit,
                    "capture_private_memory_increment_exceeded");
            capture_peak_delta = (std::max)(capture_peak_delta, capture_delta);
        }

        std::printf("memory_budget_test: sample_ms=%lu pairs=%zu coordinator_peak=%llu "
                    "coordinator_limit=%llu capture_peak=%llu capture_limit=%llu "
                    "queue_bytes=%zu queue_limit=%llu\n",
                    static_cast<unsigned long>(sample_interval_ms), measurement_pairs,
                    static_cast<unsigned long long>(coordinator_peak_delta),
                    static_cast<unsigned long long>(coordinator_limit),
                    static_cast<unsigned long long>(capture_peak_delta),
                    static_cast<unsigned long long>(capture_increment_limit),
                    sizeof(ProductionBridge), static_cast<unsigned long long>(queue_limit));
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "memory_budget_test: %s\n", error.what());
        return 1;
    }
}
