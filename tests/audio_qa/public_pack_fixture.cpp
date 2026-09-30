#include <ayther/ayther_core_ffi.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Detector = std::unique_ptr<AytherAudioEventDetector, decltype(&ayther_audio_event_free)>;
using Archive = std::unique_ptr<AyArchive, decltype(&ayther_pack_close)>;
using Substitutor = std::unique_ptr<AytherAudioSubstitutor, decltype(&ayther_audio_sub_free)>;

constexpr std::array<std::uint8_t, 60> known_wav{
    'R', 'I', 'F', 'F', 52, 0, 0,   0,   'W', 'A',  'V',  'E', 'f', 'm',  't',
    ' ', 16,  0,   0,   0,  1, 0,   2,   0,   0x44, 0xAC, 0,   0,   0x10, 0xB1,
    2,   0,   4,   0,   16, 0, 'd', 'a', 't', 'a',  16,   0,   0,   0,    1,
    0,   1,   0,   2,   0,  2, 0,   3,   0,   3,    0,    4,   0,   4,    0};

void require(const bool condition, const std::string_view message) {
    if (!condition)
        throw std::runtime_error{std::string{message}};
}

AytherAudioEvent controlled_event() {
    Detector detector{ayther_audio_event_new(), ayther_audio_event_free};
    require(detector != nullptr, "detector_creation_failed");
    const std::array<AytherAudioWrite, 3> key_on{
        {{100, 0x00A0, 0x55, 0}, {120, 0x00A4, 0x22, 0}, {140, 0x0028, 0xF0, 0}}};
    const std::array<AytherAudioWrite, 1> key_off{{{100, 0x0028, 0x00, 0}}};
    ayther_audio_event_process_frame(detector.get(), 1, key_on.data(),
                                     static_cast<std::uint32_t>(key_on.size()));
    ayther_audio_event_process_frame(detector.get(), 2, key_off.data(),
                                     static_cast<std::uint32_t>(key_off.size()));
    AytherAudioEvent event{};
    require(ayther_audio_event_get(detector.get(), &event, 1) == 1, "controlled_event_missing");
    require(event.signature != 0 && event.start_frame == 1 && event.end_frame == 1 &&
                event.chip == 0 && event.channel == 0,
            "controlled_event_mismatch");
    return event;
}

std::vector<std::uint8_t> read_entry(const AyArchive *const archive, const std::string &path) {
    const auto size = ayther_pack_file_size(archive, path.c_str());
    require(size >= 0, "pack_entry_missing");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    require(ayther_pack_read(archive, path.c_str(), bytes.data(), bytes.size()) == size,
            "pack_entry_read_failed");
    return bytes;
}

} // namespace

int main(const int argc, const char *const argv[]) {
    try {
        const auto event = controlled_event();
        if (argc == 2 && std::string_view{argv[1]} == "--signature") {
            std::printf("%016llx\n", static_cast<unsigned long long>(event.signature));
            return 0;
        }
        require(argc == 3, "usage: public_pack_fixture <pack> <registry>");
        Archive archive{ayther_pack_open_trusted(argv[1], argv[2]), ayther_pack_close};
        require(archive != nullptr, "trusted_pack_open_failed");

        std::array<char, 33> asset_id{};
        require(ayther_asset_id_bytes(known_wav.data(), known_wav.size(), asset_id.data(),
                                      asset_id.size()),
                "asset_id_failed");
        const std::string asset_path = "assets/" + std::string{asset_id.data()};
        require(read_entry(archive.get(), asset_path) ==
                    std::vector<std::uint8_t>{known_wav.begin(), known_wav.end()},
                "known_pcm_mismatch");

        Substitutor substitutor{ayther_audio_sub_new(), ayther_audio_sub_free};
        require(substitutor != nullptr, "substitutor_creation_failed");
        ayther_audio_sub_load_pack(substitutor.get(), archive.get());
        require(ayther_audio_sub_event_catalog_len(substitutor.get()) == 1,
                "event_catalog_size_mismatch");
        AytherAudioEventSub substitution{};
        require(ayther_audio_sub_resolve_events(substitutor.get(), &event, 1, &substitution, 1) ==
                    1,
                "controlled_assignment_not_resolved");
        require(substitution.signature == event.signature &&
                    std::string_view{substitution.asset_path} == asset_id.data() &&
                    substitution.start_frame == 1 && substitution.end_frame == 1,
                "controlled_assignment_mismatch");

        std::printf("public_pack_fixture: signature=%016llx asset=%s pcm_bytes=%zu\n",
                    static_cast<unsigned long long>(event.signature), asset_id.data(),
                    known_wav.size());
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "public_pack_fixture: %s\n", error.what());
        return 1;
    }
}
