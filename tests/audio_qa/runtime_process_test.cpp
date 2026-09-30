#include "runtime_process.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::filesystem::path executable_path() {
    std::wstring result(32768, L'\0');
    const auto length =
        GetModuleFileNameW(nullptr, result.data(), static_cast<DWORD>(result.size()));
    require(length != 0 && length < result.size(), "test_executable_path_unavailable");
    result.resize(length);
    return result;
}

int child(const int argc, char **argv) {
    if (argc != 4 || std::string_view{argv[2]} != "argument with spaces" ||
        std::string_view{argv[3]} != "quote\"inside") {
        return 10;
    }
    wchar_t allowed[32]{};
    if (GetEnvironmentVariableW(L"QA_ALLOWED", allowed, 32) == 0 ||
        std::wstring_view{allowed} != L"allowed-value") {
        return 11;
    }
    wchar_t forbidden[32]{};
    if (GetEnvironmentVariableW(L"QA_FORBIDDEN", forbidden, 32) != 0) {
        return 12;
    }
    return 0;
}

int handle_child(const char *const token) {
    auto adopted = qa::adopt_inherited_data_channel(token);
    auto *channel = std::get_if<qa::OwnedChannelHandle>(&adopted);
    if (channel == nullptr)
        return 20;
    constexpr std::string_view payload{"explicit-handle-list"};
    return qa::write_channel(*channel, std::as_bytes(std::span{payload})) ? 0 : 21;
}

int parent() {
    const auto identified = qa::identify_runtime_binary(executable_path());
    const auto *identity = std::get_if<qa::RuntimeBinaryIdentity>(&identified);
    require(identity != nullptr, "runtime_binary_identity_unavailable");

    require(SetEnvironmentVariableW(L"QA_FORBIDDEN", L"must-not-be-inherited") != FALSE,
            "parent_environment_setup_failed");
    auto launched = qa::launch_runtime_process(
        *identity, {L"--child", L"argument with spaces", L"quote\"inside"},
        {{L"QA_ALLOWED", L"allowed-value"}});
    require(SetEnvironmentVariableW(L"QA_FORBIDDEN", nullptr) != FALSE,
            "parent_environment_cleanup_failed");
    auto *process = std::get_if<qa::OwnedRuntimeProcess>(&launched);
    require(process != nullptr && static_cast<bool>(*process), "runtime_process_launch_failed");
    std::int32_t exit_code{};
    require(process->wait(10000, exit_code) && exit_code == 0,
            "runtime_process_arguments_or_environment_changed");

    auto channel_result = qa::create_inherited_data_channel();
    auto *channel = std::get_if<qa::InheritedDataChannel>(&channel_result);
    require(channel != nullptr, "runtime_process_channel_unavailable");
    const auto token = qa::inherited_data_channel_token(channel->runtime_write.get());
    const std::array<qa::NativeChannelHandle, 1> handles{channel->runtime_write.get()};
    auto inherited = qa::launch_runtime_process(
        *identity, {L"--handle-child", std::wstring{token.begin(), token.end()}}, {}, handles);
    auto *inherited_process = std::get_if<qa::OwnedRuntimeProcess>(&inherited);
    require(inherited_process != nullptr, "runtime_process_explicit_handle_launch_failed");
    channel->runtime_write.reset();
    require(inherited_process->wait(10000, exit_code) && exit_code == 0,
            "runtime_process_inherited_handle_child_failed");
    std::array<std::byte, 32> bytes{};
    std::size_t received{};
    require(qa::read_channel(channel->supervisor_read, bytes, received) ==
                    qa::ChannelReadResult::data &&
                std::string_view{reinterpret_cast<const char *>(bytes.data()), received} ==
                    "explicit-handle-list",
            "runtime_process_did_not_inherit_explicit_handle");

    auto changed = *identity;
    ++changed.size;
    const auto rejected = qa::launch_runtime_process(changed, {}, {});
    require(std::get_if<qa::RuntimeProcessError>(&rejected) != nullptr &&
                std::get<qa::RuntimeProcessError>(rejected) ==
                    qa::RuntimeProcessError::identity_mismatch,
            "changed_runtime_identity_was_launched");
    return 0;
}

} // namespace

int main(const int argc, char **argv) {
    try {
        if (argc >= 2 && std::string_view{argv[1]} == "--child") {
            return child(argc, argv);
        }
        if (argc == 3 && std::string_view{argv[1]} == "--handle-child")
            return handle_child(argv[2]);
        require(argc == 1, "unexpected_arguments");
        return parent();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "runtime_process_test: %s\n", error.what());
        return 1;
    }
}
