#include "recording_header.h"
#include "recording_replay_preparation.h"

#include <zstd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void append_u32(std::vector<std::byte> &bytes, const std::uint32_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
    }
}

std::vector<std::byte> layout_prefix(const std::uint32_t raw_state_bytes,
                                     const std::uint32_t compressed_bytes) {
    std::vector<std::byte> bytes{std::byte{0x41}, std::byte{0x52}, std::byte{0x50},
                                 std::byte{0x31}};
    append_u32(bytes, qa::current_recording_version);
    append_u32(bytes, 0U);
    append_u32(bytes, 0U);
    append_u32(bytes, 1U);
    append_u32(bytes, raw_state_bytes);
    append_u32(bytes, compressed_bytes);
    return bytes;
}

struct RestoreProbe {
    std::size_t calls{};
    bool state_matches{};
};

qa::GameStateRestoreOperationResult restore(void *const context,
                                            const std::vector<std::uint8_t> &state) {
    auto &probe = *static_cast<RestoreProbe *>(context);
    ++probe.calls;
    probe.state_matches = state.size() == qa::max_recording_state_bytes &&
                          state.front() == std::uint8_t{0x51} && state.back() == std::uint8_t{0xa7};
    return {probe.state_matches, probe.state_matches ? "ok" : "state_mismatch", {}};
}

} // namespace

int main() {
    try {
        constexpr std::uint64_t prefix_bytes = 28U;
        constexpr std::uint64_t input_bytes = sizeof(std::uint16_t);
        constexpr auto max_compressed_bytes =
            static_cast<std::uint32_t>(qa::max_recording_bytes - prefix_bytes - input_bytes);
        const auto maximum_take_header = layout_prefix(1U, max_compressed_bytes);
        const auto maximum_take =
            qa::decode_recording_layout(maximum_take_header, qa::max_recording_bytes);
        require(maximum_take.error == qa::RecordingLayoutError::none &&
                    maximum_take.layout.inputs.offset + maximum_take.layout.inputs.size ==
                        qa::max_recording_bytes,
                "maximum_take_was_not_admitted");

        const auto excessive_take =
            qa::decode_recording_layout(maximum_take_header, qa::max_recording_bytes + 1U);
        require(excessive_take.error == qa::RecordingLayoutError::take_too_large &&
                    qa::recording_layout_error_code(excessive_take.error) == "take_too_large",
                "excessive_take_was_not_rejected_before_content_read");

        std::vector<std::uint8_t> state(qa::max_recording_state_bytes, 0U);
        state.front() = 0x51U;
        state.back() = 0xa7U;
        std::vector<std::byte> compressed(1024U * 1024U);
        const auto compressed_size =
            ZSTD_compress(compressed.data(), compressed.size(), state.data(), state.size(), 1);
        require(ZSTD_isError(compressed_size) == 0U, "maximum_state_fixture_compression_failed");
        compressed.resize(compressed_size);
        state.clear();
        state.shrink_to_fit();

        auto recording = layout_prefix(qa::max_recording_state_bytes,
                                       static_cast<std::uint32_t>(compressed.size()));
        recording.insert(recording.end(), compressed.begin(), compressed.end());
        recording.push_back(std::byte{0});
        recording.push_back(std::byte{0});
        const auto maximum_state_layout = qa::decode_recording_layout(recording);
        require(maximum_state_layout.error == qa::RecordingLayoutError::none,
                "maximum_state_layout_was_not_admitted");

        RestoreProbe accepted_probe;
        const auto accepted = qa::prepare_recording_replay(recording, maximum_state_layout.layout,
                                                           &accepted_probe, &restore);
        require(accepted.state_error == qa::RecordingStateError::none &&
                    accepted.restore_attempted && accepted.restore.succeeded &&
                    accepted.inputs.has_value() && accepted_probe.calls == 1U &&
                    accepted_probe.state_matches,
                "maximum_decompressed_state_was_not_restored");

        auto excessive_state_layout = maximum_state_layout.layout;
        excessive_state_layout.raw_state_bytes = qa::max_recording_state_bytes + 1U;
        RestoreProbe rejected_probe;
        const auto rejected = qa::prepare_recording_replay(recording, excessive_state_layout,
                                                           &rejected_probe, &restore);
        require(rejected.state_error == qa::RecordingStateError::invalid_layout &&
                    !rejected.restore_attempted && !rejected.inputs.has_value() &&
                    rejected_probe.calls == 0U,
                "excessive_state_reached_allocation_or_restore_boundary");

        std::puts("recording_size_limits_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "recording_size_limits_test: %s\n", error.what());
        return 1;
    }
}
