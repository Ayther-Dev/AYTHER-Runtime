#include "inherited_channel.h"
#include "separated_channel_drain.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
        if (value.hThread != nullptr) {
            (void)CloseHandle(value.hThread);
        }
        if (value.hProcess != nullptr) {
            (void)CloseHandle(value.hProcess);
        }
    }
};

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

qa::InheritedDataChannel channel() {
    auto created = qa::create_inherited_data_channel();
    auto *value = std::get_if<qa::InheritedDataChannel>(&created);
    require(value != nullptr, "channel_creation_failed");
    return std::move(*value);
}

std::wstring executable_path() {
    std::wstring result(32768, L'\0');
    const auto length =
        GetModuleFileNameW(nullptr, result.data(), static_cast<DWORD>(result.size()));
    require(length != 0 && length < result.size(), "test_executable_path_unavailable");
    result.resize(length);
    return result;
}

std::wstring widen(const std::string &value) { return {value.begin(), value.end()}; }

int child(const char *const fact_token, const char *const pcm_token) {
    auto facts_value = qa::adopt_inherited_data_channel(fact_token);
    auto pcm_value = qa::adopt_inherited_data_channel(pcm_token);
    auto *facts = std::get_if<qa::OwnedChannelHandle>(&facts_value);
    auto *pcm = std::get_if<qa::OwnedChannelHandle>(&pcm_value);
    if (facts == nullptr || pcm == nullptr) {
        return 10;
    }

    std::array<char, 4096> log_chunk{};
    log_chunk.fill('E');
    for (std::size_t offset = 0; offset < 1024U * 1024U; offset += log_chunk.size()) {
        DWORD written{};
        if (!WriteFile(GetStdHandle(STD_ERROR_HANDLE), log_chunk.data(),
                       static_cast<DWORD>(log_chunk.size()), &written, nullptr) ||
            written != log_chunk.size()) {
            return 11;
        }
    }

    constexpr std::string_view fact_text{"fact-batch"};
    const auto fact_bytes = std::as_bytes(std::span{fact_text});
    const std::array pcm_bytes{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}, std::byte{0x10}};
    if (!qa::write_channel(*facts, fact_bytes) || !qa::write_channel(*pcm, pcm_bytes)) {
        return 12;
    }
    return 0;
}

int parent() {
    auto facts = channel();
    auto pcm = channel();
    auto logs = channel();
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    qa::OwnedChannelHandle null_input{CreateFileW(L"NUL", GENERIC_READ,
                                                  FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    qa::OwnedChannelHandle null_output{CreateFileW(L"NUL", GENERIC_WRITE,
                                                   FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    require(null_input && null_output, "null_standard_handles_failed");

    std::array<HANDLE, 5> inherited{facts.runtime_write.get(), pcm.runtime_write.get(),
                                    logs.runtime_write.get(), null_input.get(), null_output.get()};
    SIZE_T bytes{};
    (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<std::byte> storage(bytes);
    auto *list = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    require(bytes != 0 && InitializeProcThreadAttributeList(list, 1, 0, &bytes) != FALSE &&
                UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                          inherited.data(), sizeof(inherited), nullptr,
                                          nullptr) != FALSE,
            "handle_list_setup_failed");
    struct AttributeCleanup final {
        PPROC_THREAD_ATTRIBUTE_LIST value;
        ~AttributeCleanup() { DeleteProcThreadAttributeList(value); }
    } cleanup{list};

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = null_input.get();
    startup.StartupInfo.hStdOutput = null_output.get();
    startup.StartupInfo.hStdError = logs.runtime_write.get();
    startup.lpAttributeList = list;
    std::wstring command = L"\"" + executable_path() + L"\" --child " +
                           widen(qa::inherited_data_channel_token(facts.runtime_write.get())) +
                           L" " + widen(qa::inherited_data_channel_token(pcm.runtime_write.get()));
    ProcessInformation process;
    require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                           EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup.StartupInfo, &process.value) != FALSE,
            "child_launch_failed");

    facts.runtime_write.reset();
    pcm.runtime_write.reset();
    logs.runtime_write.reset();
    null_input.reset();
    null_output.reset();
    qa::SeparatedChannelDrain drain{std::move(facts.supervisor_read),
                                    std::move(pcm.supervisor_read),
                                    std::move(logs.supervisor_read),
                                    {1024, 1024, 1024U * 1024U}};
    require(drain.start(), "channel_drain_start_failed");
    require(WaitForSingleObject(process.value.hProcess, 10000) == WAIT_OBJECT_0,
            "child_blocked_by_full_stderr");
    DWORD exit_code{};
    require(GetExitCodeProcess(process.value.hProcess, &exit_code) != FALSE && exit_code == 0,
            "child_process_failed");
    drain.join();

    const auto &fact_result = drain.result(qa::DrainedChannel::facts);
    const auto &pcm_result = drain.result(qa::DrainedChannel::pcm);
    const auto &log_result = drain.result(qa::DrainedChannel::logs);
    constexpr std::string_view expected_fact{"fact-batch"};
    const std::array expected_pcm{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff},
                                  std::byte{0x10}};
    require(!fact_result.read_failed && !fact_result.overflow &&
                std::string_view{reinterpret_cast<const char *>(fact_result.bytes.data()),
                                 fact_result.bytes.size()} == expected_fact &&
                !pcm_result.read_failed && !pcm_result.overflow &&
                pcm_result.bytes ==
                    std::vector<std::byte>{expected_pcm.begin(), expected_pcm.end()} &&
                !log_result.read_failed && !log_result.overflow &&
                log_result.bytes.size() == 1024U * 1024U,
            "drained_channels_were_blocked_or_mixed");
    return 0;
}

} // namespace

int main(const int argc, char **argv) {
    try {
        if (argc == 4 && std::string_view{argv[1]} == "--child") {
            return child(argv[2], argv[3]);
        }
        require(argc == 1, "unexpected_arguments");
        return parent();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "separated_channel_drain_test: %s\n", error.what());
        return 1;
    }
}
