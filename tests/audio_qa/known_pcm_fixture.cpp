#include "known_pcm.h"

#include <bit>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char *argv[]) {
    if (argc != 2 || argv[1][0] == '\0') {
        std::cerr << "invalid_arguments: expected a new PCM output path\n";
        return 2;
    }

    namespace fixture = ayther::runtime::audio_qa;
    try {
        const std::filesystem::path path{argv[1]};
        if (std::filesystem::exists(path)) {
            std::cerr << "output_exists: refusing to replace an existing file\n";
            return 1;
        }
        constexpr auto pcm = fixture::make_known_pcm();
        std::array<char, pcm.size() * 2> bytes{};
        for (std::size_t index = 0; index < pcm.size(); ++index) {
            const auto word = static_cast<std::uint16_t>(pcm[index]);
            bytes[index * 2] = std::bit_cast<char>(static_cast<std::uint8_t>(word & 0xffU));
            bytes[index * 2 + 1] = std::bit_cast<char>(static_cast<std::uint8_t>(word >> 8U));
        }
        std::ofstream output;
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output.open(path, std::ios::binary | std::ios::out);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();

        std::cout << "schema_version = 1\nfixture_id = \"" << fixture::fixture_id
                  << "\"\nrole = \"controlled_input\"\nformat = \"s16le\"\nsample_rate = "
                  << fixture::sample_rate << "\nchannels = " << fixture::channels
                  << "\nchannel_order = \"left,right\"\nsample_begin = 0\nsample_count = "
                  << fixture::sample_frames << '\n';
        if (!std::cout) {
            std::cerr << "metadata_write_failed\n";
            return 1;
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "pcm_write_failed: " << error.what() << '\n';
        return 1;
    }
}
