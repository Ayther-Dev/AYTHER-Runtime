#pragma once

#include "inherited_channel.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

struct RuntimeBinaryIdentity {
    std::filesystem::path path;
    std::uint64_t volume_serial{};
    std::uint64_t file_index{};
    std::uint64_t size{};
    std::uint64_t last_write{};
    bool operator==(const RuntimeBinaryIdentity &) const = default;
};

enum class RuntimeProcessError {
    invalid_path,
    identity_unavailable,
    identity_mismatch,
    invalid_argument,
    invalid_environment,
    launch_failed,
    unsupported_platform,
};

enum class RuntimeQueryError {
    invalid_request,
    identity_mismatch,
    launch_failed,
    timed_out,
    output_too_large,
    io_failed,
    unsupported_platform,
};

struct RuntimeQueryOutput {
    std::int32_t exit_code{};
    std::string output;
};

using RuntimeQueryResult = std::variant<RuntimeQueryOutput, RuntimeQueryError>;

using RuntimeBinaryIdentityResult = std::variant<RuntimeBinaryIdentity, RuntimeProcessError>;

#ifdef _WIN32
using NativeRuntimeProcessHandle = void *;
inline constexpr NativeRuntimeProcessHandle invalid_runtime_process_handle = nullptr;
#else
using NativeRuntimeProcessHandle = int;
inline constexpr NativeRuntimeProcessHandle invalid_runtime_process_handle = -1;
#endif

class OwnedRuntimeProcess final {
  public:
    OwnedRuntimeProcess() noexcept = default;
    explicit OwnedRuntimeProcess(NativeRuntimeProcessHandle handle) noexcept : handle_(handle) {}
    ~OwnedRuntimeProcess();
    OwnedRuntimeProcess(const OwnedRuntimeProcess &) = delete;
    OwnedRuntimeProcess &operator=(const OwnedRuntimeProcess &) = delete;
    OwnedRuntimeProcess(OwnedRuntimeProcess &&other) noexcept;
    OwnedRuntimeProcess &operator=(OwnedRuntimeProcess &&other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] bool wait(std::uint32_t timeout_ms, std::int32_t &exit_code) noexcept;

  private:
    NativeRuntimeProcessHandle handle_{invalid_runtime_process_handle};
};

using RuntimeEnvironmentEntry = std::pair<std::wstring, std::wstring>;
using RuntimeLaunchResult = std::variant<OwnedRuntimeProcess, RuntimeProcessError>;

[[nodiscard]] RuntimeBinaryIdentityResult
identify_runtime_binary(const std::filesystem::path &path) noexcept;

[[nodiscard]] RuntimeLaunchResult
launch_runtime_process(const RuntimeBinaryIdentity &identity,
                       const std::vector<std::wstring> &arguments,
                       std::vector<RuntimeEnvironmentEntry> environment,
                       std::span<const NativeChannelHandle> inherited_handles = {}) noexcept;

[[nodiscard]] RuntimeQueryResult
query_runtime_process(const RuntimeBinaryIdentity &identity,
                      const std::vector<std::wstring> &arguments,
                      std::vector<RuntimeEnvironmentEntry> environment,
                      std::size_t max_output_bytes, std::uint32_t timeout_ms) noexcept;

} // namespace ayther::audio_qa
