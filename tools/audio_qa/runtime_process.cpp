#include "runtime_process.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ayther::audio_qa {
namespace {

#ifdef _WIN32

bool valid_handle(const HANDLE handle) noexcept {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

std::wstring quote_argument(const std::wstring &argument) {
    if (argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring result{L'\"'};
    std::size_t backslashes{};
    for (const auto character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2U, L'\\');
    result.push_back(L'\"');
    return result;
}

bool valid_text(const std::wstring &value) noexcept {
    return value.find(L'\0') == std::wstring::npos;
}

bool valid_environment_key(const std::wstring &key) noexcept {
    return !key.empty() && valid_text(key) && key.find(L'=') == std::wstring::npos;
}

std::vector<wchar_t> environment_block(std::vector<RuntimeEnvironmentEntry> environment) {
    std::sort(environment.begin(), environment.end(), [](const auto &left, const auto &right) {
        return _wcsicmp(left.first.c_str(), right.first.c_str()) < 0;
    });
    std::vector<wchar_t> result;
    for (const auto &[key, value] : environment) {
        result.insert(result.end(), key.begin(), key.end());
        result.push_back(L'=');
        result.insert(result.end(), value.begin(), value.end());
        result.push_back(L'\0');
    }
    result.push_back(L'\0');
    if (environment.empty()) {
        result.push_back(L'\0');
    }
    return result;
}

RuntimeBinaryIdentityResult identify(const std::filesystem::path &path) noexcept {
    std::error_code error;
    const auto absolute = std::filesystem::weakly_canonical(path, error);
    if (error || absolute.empty() || !absolute.is_absolute()) {
        return RuntimeProcessError::invalid_path;
    }
    const HANDLE file = CreateFileW(absolute.c_str(), FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!valid_handle(file)) {
        return RuntimeProcessError::identity_unavailable;
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const bool inspected = GetFileInformationByHandle(file, &information) != FALSE;
    (void)CloseHandle(file);
    if (!inspected || (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return RuntimeProcessError::identity_unavailable;
    }
    return RuntimeBinaryIdentity{
        absolute, information.dwVolumeSerialNumber,
        (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) | information.nFileIndexLow,
        (static_cast<std::uint64_t>(information.nFileSizeHigh) << 32U) | information.nFileSizeLow,
        (static_cast<std::uint64_t>(information.ftLastWriteTime.dwHighDateTime) << 32U) |
            information.ftLastWriteTime.dwLowDateTime};
}

#endif

} // namespace

OwnedRuntimeProcess::~OwnedRuntimeProcess() {
#ifdef _WIN32
    if (handle_ != invalid_runtime_process_handle) {
        (void)CloseHandle(static_cast<HANDLE>(handle_));
    }
#endif
}

OwnedRuntimeProcess::OwnedRuntimeProcess(OwnedRuntimeProcess &&other) noexcept
    : handle_(other.handle_) {
    other.handle_ = invalid_runtime_process_handle;
}

OwnedRuntimeProcess &OwnedRuntimeProcess::operator=(OwnedRuntimeProcess &&other) noexcept {
    if (this != &other) {
#ifdef _WIN32
        if (handle_ != invalid_runtime_process_handle) {
            (void)CloseHandle(static_cast<HANDLE>(handle_));
        }
#endif
        handle_ = other.handle_;
        other.handle_ = invalid_runtime_process_handle;
    }
    return *this;
}

OwnedRuntimeProcess::operator bool() const noexcept {
    return handle_ != invalid_runtime_process_handle;
}

bool OwnedRuntimeProcess::wait(const std::uint32_t timeout_ms, std::int32_t &exit_code) noexcept {
#ifdef _WIN32
    if (!*this || WaitForSingleObject(static_cast<HANDLE>(handle_), timeout_ms) != WAIT_OBJECT_0) {
        return false;
    }
    DWORD native_exit{};
    if (!GetExitCodeProcess(static_cast<HANDLE>(handle_), &native_exit)) {
        return false;
    }
    exit_code = static_cast<std::int32_t>(native_exit);
    return true;
#else
    (void)timeout_ms;
    (void)exit_code;
    return false;
#endif
}

RuntimeBinaryIdentityResult identify_runtime_binary(const std::filesystem::path &path) noexcept {
#ifdef _WIN32
    return identify(path);
#else
    (void)path;
    return RuntimeProcessError::unsupported_platform;
#endif
}

RuntimeLaunchResult
launch_runtime_process(const RuntimeBinaryIdentity &identity,
                       const std::vector<std::wstring> &arguments,
                       std::vector<RuntimeEnvironmentEntry> environment,
                       const std::span<const NativeChannelHandle> inherited_handles) noexcept {
#ifdef _WIN32
    const auto current = identify(identity.path);
    const auto *current_identity = std::get_if<RuntimeBinaryIdentity>(&current);
    if (current_identity == nullptr || *current_identity != identity) {
        return RuntimeProcessError::identity_mismatch;
    }
    if (std::any_of(arguments.begin(), arguments.end(),
                    [](const auto &argument) { return !valid_text(argument); })) {
        return RuntimeProcessError::invalid_argument;
    }
    if (std::any_of(environment.begin(), environment.end(), [](const auto &entry) {
            return !valid_environment_key(entry.first) || !valid_text(entry.second);
        })) {
        return RuntimeProcessError::invalid_environment;
    }
    for (auto left = environment.begin(); left != environment.end(); ++left) {
        if (std::any_of(std::next(left), environment.end(), [&](const auto &right) {
                return _wcsicmp(left->first.c_str(), right.first.c_str()) == 0;
            })) {
            return RuntimeProcessError::invalid_environment;
        }
    }
    if (std::any_of(inherited_handles.begin(), inherited_handles.end(),
                    [](const NativeChannelHandle handle) {
                        return !channel_handle_is_inheritable(handle);
                    })) {
        return RuntimeProcessError::invalid_argument;
    }

    std::wstring command = quote_argument(identity.path.wstring());
    for (const auto &argument : arguments) {
        command.push_back(L' ');
        command.append(quote_argument(argument));
    }
    auto block = environment_block(std::move(environment));
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    std::vector<std::byte> attribute_storage;
    if (!inherited_handles.empty()) {
        SIZE_T bytes{};
        (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (bytes == 0U)
            return RuntimeProcessError::launch_failed;
        attribute_storage.resize(bytes);
        startup.lpAttributeList =
            reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
        if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes))
            return RuntimeProcessError::launch_failed;
        const auto cleanup = [&startup] { DeleteProcThreadAttributeList(startup.lpAttributeList); };
        if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0,
                                       PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       const_cast<NativeChannelHandle *>(inherited_handles.data()),
                                       inherited_handles.size_bytes(), nullptr, nullptr)) {
            cleanup();
            return RuntimeProcessError::launch_failed;
        }

        PROCESS_INFORMATION process{};
        const bool created =
            CreateProcessW(identity.path.c_str(), command.data(), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT |
                               EXTENDED_STARTUPINFO_PRESENT,
                           block.data(), identity.path.parent_path().c_str(), &startup.StartupInfo,
                           &process) != FALSE;
        cleanup();
        if (!created)
            return RuntimeProcessError::launch_failed;
        (void)CloseHandle(process.hThread);
        return OwnedRuntimeProcess{process.hProcess};
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(identity.path.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, block.data(),
                        identity.path.parent_path().c_str(), &startup.StartupInfo, &process)) {
        return RuntimeProcessError::launch_failed;
    }
    (void)CloseHandle(process.hThread);
    return OwnedRuntimeProcess{process.hProcess};
#else
    (void)identity;
    (void)arguments;
    (void)environment;
    (void)inherited_handles;
    return RuntimeProcessError::unsupported_platform;
#endif
}

RuntimeQueryResult query_runtime_process(const RuntimeBinaryIdentity &identity,
                                         const std::vector<std::wstring> &arguments,
                                         std::vector<RuntimeEnvironmentEntry> environment,
                                         const std::size_t max_output_bytes,
                                         const std::uint32_t timeout_ms) noexcept {
#ifdef _WIN32
    if (max_output_bytes == 0U || timeout_ms == 0U ||
        std::any_of(arguments.begin(), arguments.end(),
                    [](const auto &argument) { return !valid_text(argument); }) ||
        std::any_of(environment.begin(), environment.end(), [](const auto &entry) {
            return !valid_environment_key(entry.first) || !valid_text(entry.second);
        })) {
        return RuntimeQueryError::invalid_request;
    }
    for (auto left = environment.begin(); left != environment.end(); ++left) {
        if (std::any_of(std::next(left), environment.end(), [&](const auto &right) {
                return _wcsicmp(left->first.c_str(), right.first.c_str()) == 0;
            })) {
            return RuntimeQueryError::invalid_request;
        }
    }
    const auto current = identify(identity.path);
    const auto *current_identity = std::get_if<RuntimeBinaryIdentity>(&current);
    if (current_identity == nullptr || *current_identity != identity) {
        return RuntimeQueryError::identity_mismatch;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read_handle{};
    HANDLE write_handle{};
    if (!CreatePipe(&read_handle, &write_handle, &security, 0) ||
        !SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0)) {
        if (valid_handle(read_handle))
            (void)CloseHandle(read_handle);
        if (valid_handle(write_handle))
            (void)CloseHandle(write_handle);
        return RuntimeQueryError::launch_failed;
    }
    OwnedChannelHandle output_read{read_handle};
    OwnedChannelHandle output_write{write_handle};
    const HANDLE null_input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!valid_handle(null_input)) {
        return RuntimeQueryError::launch_failed;
    }
    OwnedChannelHandle input{null_input};

    std::wstring command = quote_argument(identity.path.wstring());
    for (const auto &argument : arguments) {
        command.push_back(L' ');
        command.append(quote_argument(argument));
    }
    auto block = environment_block(std::move(environment));
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = null_input;
    startup.StartupInfo.hStdOutput = write_handle;
    startup.StartupInfo.hStdError = write_handle;
    SIZE_T attribute_bytes{};
    (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
    if (attribute_bytes == 0U) {
        return RuntimeQueryError::launch_failed;
    }
    std::vector<std::byte> attribute_storage(attribute_bytes);
    startup.lpAttributeList =
        reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_bytes)) {
        return RuntimeQueryError::launch_failed;
    }
    const auto delete_attributes = [&startup] {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    };
    std::array<HANDLE, 2> child_handles{null_input, write_handle};
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   child_handles.data(), child_handles.size() * sizeof(HANDLE),
                                   nullptr, nullptr)) {
        delete_attributes();
        return RuntimeQueryError::launch_failed;
    }
    PROCESS_INFORMATION process{};
    const bool created =
        CreateProcessW(identity.path.c_str(), command.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                       block.data(), identity.path.parent_path().c_str(), &startup.StartupInfo,
                       &process) != FALSE;
    delete_attributes();
    if (!created) {
        return RuntimeQueryError::launch_failed;
    }
    (void)CloseHandle(process.hThread);
    OwnedRuntimeProcess process_owner{process.hProcess};
    output_write.reset();
    input.reset();

    RuntimeQueryOutput result;
    result.output.reserve(std::min<std::size_t>(max_output_bytes, 4096U));
    const auto started = std::chrono::steady_clock::now();
    std::array<char, 4096> buffer{};
    bool process_exited{};
    for (;;) {
        DWORD available{};
        if (!PeekNamedPipe(read_handle, nullptr, 0, nullptr, &available, nullptr)) {
            if (GetLastError() != ERROR_BROKEN_PIPE) {
                (void)TerminateProcess(process.hProcess, 1U);
                (void)WaitForSingleObject(process.hProcess, 1000U);
                return RuntimeQueryError::io_failed;
            }
            available = 0U;
        }
        while (available != 0U) {
            const auto requested =
                static_cast<DWORD>(std::min<std::size_t>(buffer.size(), available));
            DWORD bytes_read{};
            if (!ReadFile(read_handle, buffer.data(), requested, &bytes_read, nullptr) ||
                bytes_read == 0U) {
                (void)TerminateProcess(process.hProcess, 1U);
                (void)WaitForSingleObject(process.hProcess, 1000U);
                return RuntimeQueryError::io_failed;
            }
            if (result.output.size() + bytes_read > max_output_bytes) {
                (void)TerminateProcess(process.hProcess, 1U);
                (void)WaitForSingleObject(process.hProcess, 1000U);
                return RuntimeQueryError::output_too_large;
            }
            result.output.append(buffer.data(), bytes_read);
            available -= bytes_read;
        }

        if (process_exited) {
            DWORD exit_code{};
            if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
                return RuntimeQueryError::io_failed;
            }
            result.exit_code = static_cast<std::int32_t>(exit_code);
            return result;
        }

        const DWORD wait_result = WaitForSingleObject(process.hProcess, 10U);
        if (wait_result == WAIT_OBJECT_0) {
            process_exited = true;
            continue;
        }
        if (wait_result == WAIT_FAILED) {
            return RuntimeQueryError::io_failed;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        if (elapsed.count() >= timeout_ms) {
            (void)TerminateProcess(process.hProcess, 1U);
            (void)WaitForSingleObject(process.hProcess, 1000U);
            return RuntimeQueryError::timed_out;
        }
    }
#else
    (void)identity;
    (void)arguments;
    (void)environment;
    (void)max_output_bytes;
    (void)timeout_ms;
    return RuntimeQueryError::unsupported_platform;
#endif
}

} // namespace ayther::audio_qa
