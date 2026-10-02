#include "cancellation_message.h"
#include "inherited_channel.h"
#include "protocol_header.h"
#include "replay_cancellation.h"

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

qa::InheritedControlChannel control_channel() {
    auto created = qa::create_inherited_control_channel();
    auto *value = std::get_if<qa::InheritedControlChannel>(&created);
    require(value != nullptr, "control_channel_creation_failed");
    return std::move(*value);
}

qa::InheritedDataChannel data_channel() {
    auto created = qa::create_inherited_data_channel();
    auto *value = std::get_if<qa::InheritedDataChannel>(&created);
    require(value != nullptr, "data_channel_creation_failed");
    return std::move(*value);
}

bool read_exact(qa::OwnedChannelHandle &channel, const std::span<std::byte> destination) {
    std::size_t cursor{};
    while (cursor < destination.size()) {
        std::size_t received{};
        if (qa::read_channel(channel, destination.subspan(cursor), received) !=
                qa::ChannelReadResult::data ||
            received == 0) {
            return false;
        }
        cursor += received;
    }
    return true;
}

std::vector<std::byte> read_message(qa::OwnedChannelHandle &channel,
                                    const std::uint32_t expected_channel) {
    std::vector<std::byte> result(qa::protocol_header_bytes);
    require(read_exact(channel, result), "message_header_read_failed");
    const auto header = qa::decode_protocol_header(result, expected_channel);
    require(header.error == qa::HeaderError::none, "message_header_decode_failed");
    const auto previous = result.size();
    result.resize(previous + header.header.payload_bytes);
    require(read_exact(channel, std::span{result}.subspan(previous)),
            "message_payload_read_failed");
    return result;
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

qa::RunLifecycle playing_lifecycle() {
    qa::Run run;
    run.run_id = "run-121";
    run.request_id = "request-121";
    run.take_id = "take-main";
    qa::RunLifecycle lifecycle{run};
    if (lifecycle.mark_ready() != qa::RunTransitionError::none ||
        lifecycle.start_playback() != qa::RunTransitionError::none) {
        throw std::runtime_error("lifecycle_fixture_failed");
    }
    return lifecycle;
}

int child(const char *const input_token, const char *const output_token) {
    auto input_value = qa::adopt_inherited_data_channel(input_token);
    auto output_value = qa::adopt_inherited_data_channel(output_token);
    auto *input = std::get_if<qa::OwnedChannelHandle>(&input_value);
    auto *output = std::get_if<qa::OwnedChannelHandle>(&output_value);
    if (input == nullptr || output == nullptr) {
        return 10;
    }
    const auto request_bytes = read_message(*input, 1);
    const auto decoded =
        qa::decode_cancellation_message(request_bytes, qa::CancellationStage::requested, 1);
    const auto *request = std::get_if<qa::CancellationMessage>(&decoded);
    if (request == nullptr) {
        return 11;
    }

    const qa::CancellationMessage acknowledged{
        request->request_id, request->run_id, qa::CancellationStage::acknowledged, 0, {}};
    const auto acknowledged_bytes = qa::encode_cancellation_message(acknowledged, 1);
    const auto *acknowledged_payload = std::get_if<std::vector<std::byte>>(&acknowledged_bytes);
    if (acknowledged_payload == nullptr || !qa::write_channel(*output, *acknowledged_payload)) {
        return 12;
    }

    qa::ReplayControlGate controls;
    (void)controls.handle({qa::ReplayExternalControl::cancel, 0});
    qa::ReplayCancellation cancellation;
    auto lifecycle = playing_lifecycle();
    if (!cancellation.capture_request(controls, 1, std::uint64_t{901}) ||
        !cancellation.apply_at_frame_boundary(1, std::uint64_t{901}, lifecycle)) {
        return 13;
    }
    const qa::CancellationMessage applied{request->request_id, request->run_id,
                                          qa::CancellationStage::applied, 1, std::uint64_t{901}};
    const auto applied_bytes = qa::encode_cancellation_message(applied, 1);
    const auto *applied_payload = std::get_if<std::vector<std::byte>>(&applied_bytes);
    if (applied_payload == nullptr || !qa::write_channel(*output, *applied_payload)) {
        return 14;
    }
    return 0;
}

int parent() {
    auto control = control_channel();
    auto status = data_channel();
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

    std::array<HANDLE, 4> inherited{control.runtime_read.get(), status.runtime_write.get(),
                                    null_input.get(), null_output.get()};
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
    startup.StartupInfo.hStdError = null_output.get();
    startup.lpAttributeList = list;
    std::wstring command = L"\"" + executable_path() + L"\" --child " +
                           widen(qa::inherited_data_channel_token(control.runtime_read.get())) +
                           L" " +
                           widen(qa::inherited_data_channel_token(status.runtime_write.get()));
    ProcessInformation process;
    require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                           EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup.StartupInfo, &process.value) != FALSE,
            "child_launch_failed");
    control.runtime_read.reset();
    status.runtime_write.reset();
    null_input.reset();
    null_output.reset();

    const qa::CancellationMessage requested{
        "request-121", "run-121", qa::CancellationStage::requested, 0, {}};
    const auto encoded = qa::encode_cancellation_message(requested, 1);
    const auto *request_bytes = std::get_if<std::vector<std::byte>>(&encoded);
    require(request_bytes != nullptr && qa::write_channel(control.supervisor_write, *request_bytes),
            "cancel_request_delivery_failed");
    control.supervisor_write.reset();

    const auto acknowledgement_bytes = read_message(status.supervisor_read, 1);
    const auto application_bytes = read_message(status.supervisor_read, 2);
    const auto acknowledgement = qa::decode_cancellation_message(
        acknowledgement_bytes, qa::CancellationStage::acknowledged, 1);
    const auto application =
        qa::decode_cancellation_message(application_bytes, qa::CancellationStage::applied, 1);
    require(std::get_if<qa::CancellationMessage>(&acknowledgement) != nullptr &&
                std::get<qa::CancellationMessage>(acknowledgement).stage ==
                    qa::CancellationStage::acknowledged &&
                std::get_if<qa::CancellationMessage>(&application) != nullptr &&
                std::get<qa::CancellationMessage>(application).stage ==
                    qa::CancellationStage::applied &&
                std::get<qa::CancellationMessage>(application).frames_completed == 1 &&
                std::get<qa::CancellationMessage>(application).last_engine_frame ==
                    std::uint64_t{901},
            "acknowledgement_or_application_was_lost");

    require(WaitForSingleObject(process.value.hProcess, 10000) == WAIT_OBJECT_0,
            "runtime_child_did_not_cease");
    DWORD exit_code{};
    require(GetExitCodeProcess(process.value.hProcess, &exit_code) != FALSE && exit_code == 0,
            "runtime_child_failed");
    const qa::CancellationMessage ceased{"request-121", "run-121",
                                         qa::CancellationStage::cessation_confirmed, 1,
                                         std::uint64_t{901}};
    const auto ceased_bytes = qa::encode_cancellation_message(ceased, 2);
    const auto *ceased_payload = std::get_if<std::vector<std::byte>>(&ceased_bytes);
    require(ceased_payload != nullptr &&
                std::get<qa::CancellationMessage>(qa::decode_cancellation_message(
                    *ceased_payload, qa::CancellationStage::cessation_confirmed, 2)) == ceased,
            "cessation_confirmation_was_not_distinct");
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
        std::fprintf(stderr, "cancellation_message_test: %s\n", error.what());
        return 1;
    }
}
