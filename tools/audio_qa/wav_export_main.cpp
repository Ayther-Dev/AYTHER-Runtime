#include "pcm_block_store.h"
#include "wav_derivation.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace qa = ayther::audio_qa;

int main(const int argc, char *argv[]) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: ayther_audio_qa_wav_export PCM_DIRECTORY OUTPUT_DIRECTORY\n");
        return 2;
    }
    const std::filesystem::path source{argv[1]};
    const std::filesystem::path output{argv[2]};
    std::error_code error;
    std::filesystem::create_directories(output, error);
    if (error)
        return 3;

    std::vector<std::filesystem::path> blocks;
    for (std::filesystem::directory_iterator iterator{source, error}, end;
         !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_regular_file(error) && iterator->path().extension() == ".aqp")
            blocks.push_back(iterator->path());
    }
    if (error || blocks.empty())
        return 4;
    std::ranges::sort(blocks);

    std::vector<std::filesystem::path> part;
    std::size_t part_bytes{};
    std::size_t part_number{1U};
    const auto publish = [&]() -> bool {
        if (part.empty())
            return true;
        const auto name = "golden-axe-a18-part-" + std::to_string(part_number++) + ".wav";
        const auto result = qa::derive_wav(part, output / name);
        const auto *derived = std::get_if<qa::DerivedWav>(&result);
        if (derived == nullptr)
            return false;
        std::printf("wav_export: %s blocks=%zu samples=[%llu,%llu) bytes=%llu\n", name.c_str(),
                    derived->block_count, static_cast<unsigned long long>(derived->range.begin),
                    static_cast<unsigned long long>(derived->range.end),
                    static_cast<unsigned long long>(derived->publication.identity().byte_size));
        part.clear();
        part_bytes = 0U;
        return true;
    };

    for (const auto &path : blocks) {
        const auto opened = qa::read_pcm_block(path);
        const auto *block = std::get_if<qa::StoredPcmBlock>(&opened);
        if (block == nullptr || block->chunk.bytes.empty() ||
            block->chunk.bytes.size() > qa::max_derived_wav_pcm_bytes)
            return 5;
        if (!part.empty() &&
            block->chunk.bytes.size() > qa::max_derived_wav_pcm_bytes - part_bytes && !publish())
            return 6;
        part.push_back(path);
        part_bytes += block->chunk.bytes.size();
    }
    if (!publish())
        return 6;
    std::printf("wav_export: complete parts=%zu\n", part_number - 1U);
    return 0;
}
