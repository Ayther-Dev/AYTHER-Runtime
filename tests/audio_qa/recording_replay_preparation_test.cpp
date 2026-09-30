#include "recording_header.h"
#include "recording_replay_preparation.h"

#include <zstd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void append_u32(std::vector<std::byte> &bytes, const std::uint32_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
    }
}

void append_u16(std::vector<std::byte> &bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>(value >> 8U));
}

std::vector<std::byte> recording() {
    constexpr std::array state{std::uint8_t{0x10}, std::uint8_t{0x20}, std::uint8_t{0x30},
                               std::uint8_t{0x40}};
    std::vector<std::byte> compressed(ZSTD_compressBound(state.size()));
    const auto compressed_size =
        ZSTD_compress(compressed.data(), compressed.size(), state.data(), state.size(), 1);
    require(ZSTD_isError(compressed_size) == 0U, "fixture_state_compression_failed");
    compressed.resize(compressed_size);

    std::vector<std::byte> bytes{std::byte{0x41}, std::byte{0x52}, std::byte{0x50},
                                 std::byte{0x31}};
    append_u32(bytes, qa::current_recording_version);
    append_u32(bytes, 0);
    append_u32(bytes, 0);
    append_u32(bytes, 2);
    append_u32(bytes, static_cast<std::uint32_t>(state.size()));
    append_u32(bytes, static_cast<std::uint32_t>(compressed.size()));
    bytes.insert(bytes.end(), compressed.begin(), compressed.end());
    append_u16(bytes, 0x1234);
    append_u16(bytes, 0xabcd);
    return bytes;
}

struct RestoreProbe {
    std::vector<std::string> operations;
    bool state_matches{};
};

qa::GameStateRestoreOperationResult restore(void *const context,
                                            const std::vector<std::uint8_t> &state) {
    auto &probe = *static_cast<RestoreProbe *>(context);
    probe.operations.emplace_back("restore");
    probe.state_matches = state == std::vector<std::uint8_t>({0x10, 0x20, 0x30, 0x40});
    return {true, "ok", "restored by synthetic core"};
}

} // namespace

int main() {
    try {
        const auto bytes = recording();
        const auto layout = qa::decode_recording_layout(bytes);
        require(layout.error == qa::RecordingLayoutError::none, "fixture_layout_rejected");

        RestoreProbe probe;
        auto prepared = qa::prepare_recording_replay(bytes, layout.layout, &probe, &restore);
        require(prepared.state_error == qa::RecordingStateError::none &&
                    prepared.restore_attempted && prepared.restore.succeeded &&
                    prepared.restore.code == "ok" &&
                    prepared.restore.detail == "restored by synthetic core" &&
                    prepared.inputs.has_value() && probe.state_matches &&
                    probe.operations == std::vector<std::string>({"restore"}),
                "restore_result_not_preserved");

        const auto first = prepared.inputs->next();
        probe.operations.emplace_back("input_0");
        probe.operations.emplace_back("step_0");
        require(first.has_value() && first->frame == 0 && first->buttons == 0x1234 &&
                    probe.operations == std::vector<std::string>({"restore", "input_0", "step_0"}),
                "restore_did_not_precede_first_input_and_step");

        auto corrupted = bytes;
        corrupted[layout.layout.compressed_state.offset] ^= std::byte{0xff};
        RestoreProbe untouched;
        const auto rejected =
            qa::prepare_recording_replay(corrupted, layout.layout, &untouched, &restore);
        require(rejected.state_error == qa::RecordingStateError::decompression_failed &&
                    !rejected.restore_attempted && !rejected.inputs.has_value() &&
                    untouched.operations.empty(),
                "corrupt_state_reached_restore_operation");

        auto invalid_layout = layout.layout;
        invalid_layout.inputs.size -= 1U;
        RestoreProbe invalid_probe;
        const auto invalid =
            qa::prepare_recording_replay(bytes, invalid_layout, &invalid_probe, &restore);
        require(invalid.state_error == qa::RecordingStateError::invalid_layout &&
                    !invalid.restore_attempted && !invalid.inputs.has_value() &&
                    invalid_probe.operations.empty(),
                "invalid_inputs_mutated_game_state");

        std::puts("recording_replay_preparation_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "recording_replay_preparation_test: %s\n", error.what());
        return 1;
    }
}
