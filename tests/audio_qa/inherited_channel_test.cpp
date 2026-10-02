#include "inherited_channel.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

struct ProcessInformation final {
    PROCESS_INFORMATION value{};
    ~ProcessInformation() {
        if (value.hThread)
            CloseHandle(value.hThread);
        if (value.hProcess)
            CloseHandle(value.hProcess);
    }
};

void require(const bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

qa::InheritedDataChannel channel() {
    auto result = qa::create_inherited_data_channel();
    auto *value = std::get_if<qa::InheritedDataChannel>(&result);
    require(value != nullptr, "inherited_channel_creation_failed");
    return std::move(*value);
}

std::string read_all(qa::OwnedChannelHandle &channel_handle) {
    std::string result;
    std::array<std::byte, 128> buffer{};
    while (true) {
        std::size_t received{};
        const auto status = qa::read_channel(channel_handle, buffer, received);
        if (status == qa::ChannelReadResult::end_of_stream)
            return result;
        require(status == qa::ChannelReadResult::data && received != 0,
                "inherited_channel_read_failed");
        result.append(reinterpret_cast<const char *>(buffer.data()), received);
    }
}

std::wstring executable_path() {
    std::wstring result(32768, L'\0');
    const auto length =
        GetModuleFileNameW(nullptr, result.data(), static_cast<DWORD>(result.size()));
    require(length != 0 && length < result.size(), "test_executable_path_unavailable");
    result.resize(length);
    return result;
}

std::wstring widen_digits(const std::string &value) { return {value.begin(), value.end()}; }

int child(const char *data_token, const char *excluded_token) {
    auto adopted = qa::adopt_inherited_data_channel(data_token);
    auto *data = std::get_if<qa::OwnedChannelHandle>(&adopted);
    if (!data)
        return 10;

    const auto excluded_value =
        static_cast<std::uintptr_t>(std::strtoull(excluded_token, nullptr, 10));
    DWORD excluded_flags{};
    if (GetHandleInformation(reinterpret_cast<HANDLE>(excluded_value), &excluded_flags))
        return 11;

    constexpr std::string_view qa_message{"qa-data"};
    const auto qa_bytes = std::as_bytes(std::span{qa_message});
    if (!qa::write_channel(*data, qa_bytes))
        return 12;
    data->reset();

    constexpr std::string_view ordinary_out{"ordinary-out"};
    constexpr std::string_view ordinary_err{"ordinary-err"};
    DWORD written{};
    if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ordinary_out.data(),
                   static_cast<DWORD>(ordinary_out.size()), &written, nullptr) ||
        written != ordinary_out.size())
        return 13;
    if (!WriteFile(GetStdHandle(STD_ERROR_HANDLE), ordinary_err.data(),
                   static_cast<DWORD>(ordinary_err.size()), &written, nullptr) ||
        written != ordinary_err.size())
        return 14;
    return 0;
}

int parent() {
    auto data = channel();
    auto ordinary_out = channel();
    auto ordinary_err = channel();
    auto excluded = channel();
    require(!qa::channel_handle_is_inheritable(data.supervisor_read.get()) &&
                qa::channel_handle_is_inheritable(data.runtime_write.get()),
            "data_channel_inheritance_flags_wrong");

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    qa::OwnedChannelHandle child_input{CreateFileW(L"NUL", GENERIC_READ,
                                                   FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    require(static_cast<bool>(child_input) && qa::channel_handle_is_inheritable(child_input.get()),
            "child_stdin_creation_failed");

    std::array<HANDLE, 4> inherited{data.runtime_write.get(), child_input.get(),
                                    ordinary_out.runtime_write.get(),
                                    ordinary_err.runtime_write.get()};
    SIZE_T attribute_bytes{};
    (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
    require(attribute_bytes != 0, "attribute_list_size_failed");
    std::vector<std::byte> attribute_storage(attribute_bytes);
    auto *attribute_list = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
    require(InitializeProcThreadAttributeList(attribute_list, 1, 0, &attribute_bytes) != FALSE,
            "attribute_list_creation_failed");
    struct AttributeCleanup final {
        PPROC_THREAD_ATTRIBUTE_LIST value;
        ~AttributeCleanup() { DeleteProcThreadAttributeList(value); }
    } cleanup{attribute_list};
    require(UpdateProcThreadAttribute(attribute_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                      inherited.data(), sizeof(inherited), nullptr,
                                      nullptr) != FALSE,
            "explicit_handle_list_failed");

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = child_input.get();
    startup.StartupInfo.hStdOutput = ordinary_out.runtime_write.get();
    startup.StartupInfo.hStdError = ordinary_err.runtime_write.get();
    startup.lpAttributeList = attribute_list;

    const auto data_token = qa::inherited_data_channel_token(data.runtime_write.get());
    const auto excluded_token = qa::inherited_data_channel_token(excluded.runtime_write.get());
    std::wstring command = L"\"" + executable_path() + L"\" --child " + widen_digits(data_token) +
                           L" " + widen_digits(excluded_token);
    ProcessInformation process;
    require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                           EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup.StartupInfo, &process.value) != FALSE,
            "child_process_creation_failed");

    const auto data_write = data.runtime_write.get();
    data.runtime_write.reset();
    ordinary_out.runtime_write.reset();
    ordinary_err.runtime_write.reset();
    child_input.reset();
    require(WaitForSingleObject(process.value.hProcess, 10000) == WAIT_OBJECT_0,
            "child_process_timeout");
    DWORD exit_code{};
    require(GetExitCodeProcess(process.value.hProcess, &exit_code) && exit_code == 0,
            "child_process_failed");

    const auto qa_data = read_all(data.supervisor_read);
    const auto stdout_data = read_all(ordinary_out.supervisor_read);
    const auto stderr_data = read_all(ordinary_err.supervisor_read);
    require(qa_data == "qa-data" && stdout_data == "ordinary-out" && stderr_data == "ordinary-err",
            "qa_channel_invaded_standard_streams");

    DWORD flags{};
    require(!GetHandleInformation(data_write, &flags), "parent_retained_child_data_handle");
    const auto data_read = data.supervisor_read.get();
    data.supervisor_read.reset();
    require(!GetHandleInformation(data_read, &flags), "channel_read_handle_not_released");
    return 0;
}

} // namespace

int main(const int argc, char **argv) {
    try {
        if (argc == 4 && std::string_view{argv[1]} == "--child")
            return child(argv[2], argv[3]);
        require(argc == 1, "unexpected_arguments");
        return parent();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "inherited_channel_test: %s\n", error.what());
        return 1;
    }
}
