#include "recording_header.h"
#include "recording_input_source.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
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
    constexpr std::array inputs{std::uint16_t{0x0001}, std::uint16_t{0x80ff}, std::uint16_t{0x1234},
                                std::uint16_t{0xffff}};
    std::vector<std::byte> bytes{std::byte{0x41}, std::byte{0x52}, std::byte{0x50},
                                 std::byte{0x31}};
    append_u32(bytes, qa::current_recording_version);
    append_u32(bytes, 0);
    append_u32(bytes, 4);
    bytes.insert(bytes.end(), {std::byte{'t'}, std::byte{'a'}, std::byte{'k'}, std::byte{'e'}});
    append_u32(bytes, static_cast<std::uint32_t>(inputs.size()));
    append_u32(bytes, 1);
    append_u32(bytes, 1);
    bytes.push_back(std::byte{0x5a});
    for (const auto input : inputs) {
        append_u16(bytes, input);
    }

    append_u32(bytes, 2);                           // trim_in must not skip inputs 0 or 1.
    append_u32(bytes, 3);                           // trim_out must not remove input 3.
    bytes.insert(bytes.end(), 64, std::byte{0xa5}); // Authored history ignored.
    return bytes;
}

} // namespace

int main() {
    try {
        const auto bytes = recording();
        const auto layout = qa::decode_recording_layout(bytes);
        require(layout.error == qa::RecordingLayoutError::none, "fixture_layout_rejected");

        auto result = qa::make_recording_input_source(bytes, layout.layout);
        require(result.error == qa::RecordingInputSourceError::none &&
                    result.source.frame_count() == 4 && result.source.consumed() == 0 &&
                    !result.source.exhausted(),
                "valid_input_source_rejected");

        constexpr std::array expected{std::uint16_t{0x0001}, std::uint16_t{0x80ff},
                                      std::uint16_t{0x1234}, std::uint16_t{0xffff}};
        for (std::uint32_t frame = 0; frame < static_cast<std::uint32_t>(expected.size());
             ++frame) {
            const auto input = result.source.next();
            require(input.has_value() && input->frame == frame &&
                        input->buttons == expected[frame] && result.source.consumed() == frame + 1U,
                    "recorded_input_order_changed");
        }
        require(result.source.exhausted() && !result.source.next().has_value() &&
                    !result.source.next().has_value() &&
                    result.source.consumed() == static_cast<std::uint32_t>(expected.size()),
                "input_source_did_not_stop_at_n");

        auto invalid_layout = layout.layout;
        invalid_layout.inputs.size -= 1U;
        require(qa::make_recording_input_source(bytes, invalid_layout).error ==
                    qa::RecordingInputSourceError::invalid_input_range,
                "mismatched_input_range_accepted");

        invalid_layout = layout.layout;
        invalid_layout.inputs.offset = bytes.size();
        require(qa::make_recording_input_source(bytes, invalid_layout).error ==
                    qa::RecordingInputSourceError::truncated_input_range,
                "truncated_input_range_accepted");

        std::puts("recording_input_source_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "recording_input_source_test: %s\n", error.what());
        return 1;
    }
}
