#include "capture.h"
#include "player_config.h"
#include "runtime_options.h"
#include "status_emitter.h"
#include "vulkan_backend/spirv_file.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string bytes_as_string(const std::uint8_t* data, const std::size_t size) {
    return {reinterpret_cast<const char*>(data), size};
}

void fuzz_cli(const std::uint8_t* data, const std::size_t size) {
    std::vector<std::string> storage{"ayther_runtime"};
    std::size_t begin = 0;
    while (begin < size && storage.size() < 32) {
        std::size_t end = begin;
        while (end < size && data[end] != 0 && data[end] != '\n') {
            ++end;
        }
        storage.push_back(bytes_as_string(data + begin, end - begin));
        begin = end + 1;
    }
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (std::string& argument : storage) {
        argv.push_back(argument.data());
    }
    (void)ayther::runtime::RuntimeOptions::parse(
        static_cast<int>(argv.size()), argv.data());
}

void fuzz_player_config(const std::uint8_t* data, const std::size_t size) {
    const auto path = std::filesystem::temp_directory_path() /
                      "ayther-runtime-fuzz-player-config.toml";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(data),
                 static_cast<std::streamsize>(size));
    output.close();
    (void)ayther::player_config_load_checked(path);
}

void fuzz_status_json(const std::uint8_t* data, const std::size_t size) {
    const std::string input = bytes_as_string(data, size);
    (void)ayther::runtime::StatusEmitter::format_line(
        ayther::runtime::WarningStatus{
            ayther::runtime::RuntimeErrorCode::config_invalid, input});
}

void fuzz_metadata_json(const std::uint8_t* data, const std::size_t size) {
    const std::string input = bytes_as_string(data, size);
    ayther::CaptureMeta metadata;
    metadata.game_id = input;
    metadata.pack_name = input;
    metadata.pack_build = input;
    metadata.profile = input;
    metadata.timestamp = input;
    metadata.width = size > 0 ? data[0] : 0;
    metadata.height = size > 1 ? data[1] : 0;
    (void)ayther::capture_metadata_json(metadata);
}

void fuzz_spirv_loader(const std::uint8_t* data, const std::size_t size) {
    const auto path = std::filesystem::temp_directory_path() /
                      "ayther-runtime-fuzz-shader.spv";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(data),
                 static_cast<std::streamsize>(size));
    output.close();
    (void)ayther::runtime::vulkan::load_spirv_binary(path.string());
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      const std::size_t size) {
    if (size == 0) {
        return 0;
    }
    switch (data[0] % 5U) {
    case 0:
        fuzz_cli(data + 1, size - 1);
        break;
    case 1:
        fuzz_player_config(data + 1, size - 1);
        break;
    case 2:
        fuzz_status_json(data + 1, size - 1);
        break;
    case 3:
        fuzz_metadata_json(data + 1, size - 1);
        break;
    case 4:
        fuzz_spirv_loader(data + 1, size - 1);
        break;
    default:
        break;
    }
    return 0;
}
