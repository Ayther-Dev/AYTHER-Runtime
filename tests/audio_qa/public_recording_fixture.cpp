#include "recording_header.h"
#include "recording_replay_preparation.h"
#include "synthetic_libretro_core_api.h"

#include <zstd.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

constexpr std::array<std::uint16_t, 6> known_inputs{0x0000, 0x0001, 0x0002, 0x0004, 0x0008, 0x0801};
std::uint16_t current_input{};

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned button) {
    if (port != 0 || device != 1 || index != 0 || button >= 16)
        return 0;
    return (current_input & static_cast<std::uint16_t>(1U << button)) != 0 ? 1 : 0;
}

qa::GameStateRestoreOperationResult restore_state(void *, const std::vector<std::uint8_t> &state) {
    return retro_unserialize(state.data(), state.size())
               ? qa::GameStateRestoreOperationResult{true, "ok", "synthetic state restored"}
               : qa::GameStateRestoreOperationResult{false, "synthetic_state_rejected",
                                                     "synthetic core rejected its recorded state"};
}

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "public_recording_open_failed");
    std::vector<char> characters{std::istreambuf_iterator<char>{input},
                                 std::istreambuf_iterator<char>{}};
    require(!input.bad(), "public_recording_read_failed");
    std::vector<std::byte> bytes(characters.size());
    std::memcpy(bytes.data(), characters.data(), characters.size());
    return bytes;
}

SyntheticSerializedState state() {
    SyntheticSerializedState result;
    require(retro_serialize(&result, sizeof(result)), "synthetic_state_serialize_failed");
    return result;
}

void append_u16(std::vector<std::byte> &bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>(value >> 8U));
}

void append_u32(std::vector<std::byte> &bytes, std::uint32_t value) {
    for (std::size_t index{}; index < sizeof(value); ++index)
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
}

void append_text(std::vector<std::byte> &bytes, std::string_view text) {
    append_u32(bytes, static_cast<std::uint32_t>(text.size()));
    for (const unsigned char character : text)
        bytes.push_back(static_cast<std::byte>(character));
}

std::vector<std::byte> make_recording(const std::vector<std::uint8_t> &initial_state) {
    std::vector<std::byte> compressed(ZSTD_compressBound(initial_state.size()));
    const auto compressed_size = ZSTD_compress(compressed.data(), compressed.size(),
                                               initial_state.data(), initial_state.size(), 9);
    require(ZSTD_isError(compressed_size) == 0U, "public_recording_compression_failed");
    compressed.resize(compressed_size);

    std::vector<std::byte> bytes{std::byte{'A'}, std::byte{'R'}, std::byte{'P'}, std::byte{'1'}};
    append_u32(bytes, qa::current_recording_version);
    append_text(bytes, "ayther-public-synthetic-v1");
    append_text(bytes, "audio-qa-public-replay");
    append_u32(bytes, static_cast<std::uint32_t>(known_inputs.size()));
    append_u32(bytes, static_cast<std::uint32_t>(initial_state.size()));
    append_u32(bytes, static_cast<std::uint32_t>(compressed.size()));
    bytes.insert(bytes.end(), compressed.begin(), compressed.end());
    for (const auto input : known_inputs)
        append_u16(bytes, input);

    append_u32(bytes, 0);
    append_u32(bytes, static_cast<std::uint32_t>(known_inputs.size()));
    for (std::size_t frame{}; frame < known_inputs.size(); ++frame)
        for (std::size_t field{}; field < 6; ++field)
            append_u16(bytes, 0);
    append_u32(bytes, 0);
    for (std::size_t frame{}; frame < known_inputs.size(); ++frame)
        append_u32(bytes, 0);
    append_u32(bytes, 0);
    append_u32(bytes, 0);
    for (std::size_t frame{}; frame < known_inputs.size(); ++frame)
        append_u32(bytes, 0);
    append_u32(bytes, 1);
    return bytes;
}

void write_recording(const std::filesystem::path &path, const std::vector<std::byte> &bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "public_recording_create_failed");
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(output), "public_recording_write_failed");
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2 || argv[1] == nullptr)
        return 2;
    const std::filesystem::path output{argv[1]};
    try {
        std::filesystem::create_directories(output.parent_path());
        constexpr std::array<std::uint8_t, 16> public_rom{0x41, 0x59, 0x54, 0x48, 0x45, 0x52,
                                                          0x2d, 0x51, 0x41, 0x2d, 0x50, 0x55,
                                                          0x42, 0x4c, 0x49, 0x43};
        retro_init();
        retro_set_input_state(input_state);
        const retro_game_info game{nullptr, public_rom.data(), public_rom.size(), nullptr};
        require(retro_load_game(&game), "synthetic_core_load_failed");

        std::vector<std::uint8_t> initial_state(retro_serialize_size());
        require(initial_state.size() == sizeof(SyntheticSerializedState) &&
                    retro_serialize(initial_state.data(), initial_state.size()),
                "synthetic_initial_state_failed");
        const auto initial = state();
        require(initial.magic == synthetic_state_magic &&
                    initial.version == synthetic_state_version && initial.frame == 0,
                "synthetic_initial_state_invalid");

        const auto expected_bytes = make_recording(initial_state);
        write_recording(output, expected_bytes);
        const auto bytes = read_bytes(output);
        require(bytes == expected_bytes, "public_recording_bytes_changed");
        const auto layout = qa::decode_recording_layout(bytes);
        require(layout.error == qa::RecordingLayoutError::none &&
                    layout.layout.frame_count == known_inputs.size(),
                "public_recording_layout_rejected");
        const auto prepared =
            qa::prepare_recording_replay(bytes, layout.layout, nullptr, restore_state);
        require(prepared.state_error == qa::RecordingStateError::none &&
                    prepared.restore.succeeded && prepared.inputs,
                "public_recording_prepare_failed");

        auto inputs = *prepared.inputs;
        for (std::size_t frame{}; frame < known_inputs.size(); ++frame) {
            const auto input = inputs.next();
            require(input && input->frame == frame && input->buttons == known_inputs[frame],
                    "public_recording_input_changed");
            current_input = input->buttons;
            retro_run();
        }
        require(!inputs.next(), "public_recording_has_extra_input");
        const auto final = state();
        require(final.frame == known_inputs.size() && final.last_input == known_inputs.back() &&
                    final.accumulator != initial.accumulator,
                "synthetic_core_did_not_consume_known_inputs");
        retro_unload_game();
        retro_deinit();
        std::printf("public_recording_fixture: frames=%zu state_bytes=%zu path=%s\n",
                    known_inputs.size(), initial_state.size(), output.string().c_str());
        return 0;
    } catch (const std::exception &error) {
        retro_unload_game();
        retro_deinit();
        std::error_code ignored;
        std::filesystem::remove(output, ignored);
        std::fprintf(stderr, "public_recording_fixture: %s\n", error.what());
        return 1;
    }
}
