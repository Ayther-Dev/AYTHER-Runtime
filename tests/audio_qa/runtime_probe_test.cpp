// Spec 002: RF-1.7 and RF-2.2 (plan §5.2, contracts.md C5). The core is probed by the
// Runtime in its own process, with the ROM when it is given, and compared with the ROM
// before the request is admitted. Without arguments the test checks the reading of the
// probe; with the Runtime, the synthetic core, a library that is not libretro and the
// synthetic ROM it also runs them.
#include "runtime_probe.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

void probe_output_is_read() {
    const auto loaded = qa::parse_core_probe(
        "[Config] Runtime data: C:\\data\n"
        "AYTHER_STATUS {\"protocol_version\":1,\"event\":\"probe\",\"ok\":true,\"api\":1,"
        "\"library_name\":\"Genesis Plus GX\",\"library_version\":\"1.7.4\","
        "\"valid_extensions\":\"MD|gen|bin|smd\",\"need_fullpath\":false,"
        "\"block_extract\":false}\r\n");
    expect(loaded && loaded->loaded && loaded->library_name == "Genesis Plus GX" &&
               loaded->extensions == std::vector<std::string>{"md", "gen", "bin", "smd"} &&
               !loaded->game_loaded && !loaded->timing_fps,
           "RF-2.2: the probe event of a core is read");
    const auto with_rom = qa::parse_core_probe(
        "AYTHER_STATUS {\"protocol_version\":1,\"event\":\"probe\",\"ok\":true,\"api\":1,"
        "\"library_name\":\"core\",\"library_version\":\"1\",\"valid_extensions\":\"md\","
        "\"need_fullpath\":false,\"block_extract\":false,\"game_loaded\":true,"
        "\"timing_fps\":59.922743,\"geometry_known\":false}\n");
    expect(with_rom && with_rom->game_loaded == true && with_rom->timing_fps == 59.922743,
           "RF-2.2: the ROM probe gives the timing of the replay");
    const auto rejected_rom = qa::parse_core_probe(
        "AYTHER_STATUS {\"protocol_version\":1,\"event\":\"probe\",\"ok\":true,\"api\":1,"
        "\"library_name\":\"core\",\"library_version\":\"1\",\"valid_extensions\":\"md\","
        "\"need_fullpath\":false,\"block_extract\":false,\"game_loaded\":false,"
        "\"game_message\":\"bad header\"}\n");
    expect(rejected_rom && rejected_rom->game_loaded == false &&
               rejected_rom->game_message == "bad header" &&
               qa::core_issue(*rejected_rom, "game.md") == "core_rejects_rom",
           "RF-1.7: a core that does not load the ROM is an incompatibility in its field");
    const auto failed = qa::parse_core_probe(
        "[core-probe] not a core\n"
        "AYTHER_STATUS {\"protocol_version\":1,\"event\":\"probe\",\"ok\":false,"
        "\"reason\":\"core.invalid\",\"message\":\"C:\\\\x.dll: \\\"bad\\\"\"}\n");
    expect(failed && !failed->loaded && failed->reason == "core.invalid",
           "RF-2.2: a rejected library keeps the Runtime reason");
    expect(!qa::parse_core_probe("no status line\n") &&
               !qa::parse_core_probe("AYTHER_STATUS {\"event\":\"ready\",\"ok\":true}\n") &&
               !qa::parse_core_probe("AYTHER_STATUS {\"event\":\"probe\",\"ok\":tru}\n"),
           "RF-2.2: anything but a well-formed probe event is not a probe");
}

void core_is_compared_with_the_rom() {
    qa::CoreProbe core;
    core.loaded = true;
    core.extensions = {"md", "gen", "bin"};
    expect(!qa::core_issue(core, "C:/roms/Golden Axe.MD"),
           "RF-1.7: a core that declares the ROM extension is compatible");
    expect(qa::core_issue(core, "C:/roms/game.sfc") == "core_platform_mismatch",
           "RF-1.7: a core for another platform is an incompatibility in its field");
    core.extensions.clear();
    expect(!qa::core_issue(core, "C:/roms/game.sfc"),
           "RF-1.7: a core that declares no extension accepts any ROM");
    qa::CoreProbe invalid;
    invalid.reason = "core.invalid";
    qa::CoreProbe unloadable;
    unloadable.reason = "core.load_failed";
    expect(qa::core_issue(invalid, "game.md") == "core_invalid" &&
               qa::core_issue(unloadable, "game.md") == "core_load_failed",
           "RF-2.2: a library that is not a usable core is rejected");
}

// BR-057: the pack probe line is read strictly and a pack that cannot be used names its
// field with the Runtime reason.
void pack_probe_output_is_read() {
    const auto usable = qa::parse_pack_probe(
        "AYTHER_PACK_PROBE {\"schema\":\"1.0\",\"opened\":true,\"signature\":\"valid\","
        "\"trust\":\"trusted\",\"catalog\":{\"poses\":217,\"audio_events\":14},"
        "\"unreadable_assets\":[],\"game_id\":\"crc32:665d7df9\",\"errors\":[]}\n");
    expect(usable && usable->opened && usable->poses == 217U && usable->audio_events == 14U &&
               usable->game_id == "crc32:665d7df9" && !qa::pack_issue(*usable),
           "RF-2.2: a usable pack has no issue");
    const auto corrupt = qa::parse_pack_probe(
        "AYTHER_PACK_PROBE {\"schema\":\"1.0\",\"opened\":true,\"signature\":\"valid\","
        "\"trust\":\"trusted\",\"catalog\":{\"poses\":1,\"audio_events\":1},"
        "\"unreadable_assets\":[\"a\",\"b\"],\"game_id\":\"g\",\"errors\":[],"
        "\"reason\":\"pack_assets_unreadable\"}\n");
    expect(corrupt && corrupt->unreadable_assets == std::vector<std::string>{"a", "b"} &&
               qa::pack_issue(*corrupt) == qa::FieldIssue{"--pack", "pack_assets_unreadable"},
           "RF-2.2: undecodable assets stop the request in the --pack field");
    const auto unverified =
        qa::parse_pack_probe("AYTHER_PACK_PROBE {\"schema\":\"1.0\",\"opened\":false,\"signature\":"
                             "\"unverified\",\"trust\":\"unverified\",\"catalog\":null,"
                             "\"unreadable_assets\":[],\"game_id\":\"\",\"errors\":[],"
                             "\"reason\":\"pack_trust_unverified\",\"message\":\"m\"}\n");
    expect(unverified && !unverified->poses &&
               qa::pack_issue(*unverified) ==
                   qa::FieldIssue{"--trust-registry", "pack_trust_unverified"},
           "RF-2.2: a signed pack without a registry points at --trust-registry");
    expect(!qa::parse_pack_probe("AYTHER_PACK_PROBE {\"schema\":\"2.0\",\"opened\":true,"
                                 "\"signature\":\"valid\",\"trust\":\"trusted\"}\n") &&
               !qa::parse_pack_probe("AYTHER_PACK_PROBE {\"schema\":\"1.0\"}\n") &&
               !qa::parse_pack_probe("no probe line\n"),
           "RF-2.2: an unknown or incomplete probe line is not a probe");
}

// D-1 (campaign 2026-10-04; RF-2.2): the probe lists the profiles of the pack and a requested
// profile the pack does not offer is rejected in --profile. A Runtime that does not list them
// leaves nothing to compare.
void requested_profile_is_offered() {
    const auto listed = qa::parse_pack_probe(
        "AYTHER_PACK_PROBE {\"schema\":\"1.0\",\"opened\":true,\"signature\":\"valid\","
        "\"trust\":\"trusted\",\"catalog\":{\"poses\":1,\"audio_events\":1},"
        "\"unreadable_assets\":[],\"profiles\":[\"original\",\"full\"],\"game_id\":\"g\","
        "\"errors\":[]}\n");
    expect(listed && listed->profiles == std::vector<std::string>{"original", "full"},
           "D-1: the probe lists the profiles of the pack");
    expect(listed && !qa::profile_issue(std::string{"full"}, *listed) &&
               !qa::profile_issue(std::nullopt, *listed),
           "D-1: an offered profile, or none, is no issue");
    expect(listed && qa::profile_issue(std::string{"music"}, *listed) ==
                         qa::FieldIssue{"--profile", "profile_not_in_pack"},
           "D-1, RF-2.2: a profile the pack does not offer is rejected in --profile");
    const auto unlisted = qa::parse_pack_probe(
        "AYTHER_PACK_PROBE {\"schema\":\"1.0\",\"opened\":true,\"signature\":\"valid\","
        "\"trust\":\"trusted\",\"catalog\":null,\"unreadable_assets\":[],\"game_id\":\"g\","
        "\"errors\":[]}\n");
    expect(unlisted && !unlisted->profiles && !qa::profile_issue(std::string{"music"}, *unlisted),
           "D-1: without a list from the Runtime nothing is compared");
}

void real_pack_probes(const qa::RuntimeBinaryIdentity &runtime,
                      const std::filesystem::path &fixtures) {
    const auto registry = fixtures / "public-synthetic-trust.toml";
    const auto valid = qa::probe_pack(runtime, fixtures / "public-synthetic.ay", registry, {});
    const auto *probe = std::get_if<qa::PackProbe>(&valid);
    expect(probe != nullptr && !qa::pack_issue(*probe) &&
               probe->game_id == "ayther-public-synthetic-v1" && probe->audio_events == 1U,
           "RF-2.2: the valid public pack can be used");
    expect(probe != nullptr && probe->profiles && !probe->profiles->empty(),
           "D-1: the Runtime lists the profiles of the pack");
    const auto without_registry =
        qa::probe_pack(runtime, fixtures / "public-synthetic.ay", std::nullopt, {});
    const auto *unverified = std::get_if<qa::PackProbe>(&without_registry);
    expect(unverified != nullptr && qa::pack_issue(*unverified) ==
                                        qa::FieldIssue{"--trust-registry", "pack_trust_unverified"},
           "RF-2.2: the signed pack without a registry is never treated as «Sin pack»");
    const auto corrupt =
        qa::probe_pack(runtime, fixtures / "public-synthetic-corrupt-asset.ay", registry, {});
    const auto *corrupt_probe = std::get_if<qa::PackProbe>(&corrupt);
    expect(corrupt_probe != nullptr &&
               qa::pack_issue(*corrupt_probe) == qa::FieldIssue{"--pack", "pack_assets_unreadable"},
           "RF-2.2: a pack with an undecodable asset stops the request");
    const auto missing = qa::probe_pack(runtime, fixtures / "missing.ay", registry, {});
    const auto *missing_probe = std::get_if<qa::PackProbe>(&missing);
    expect(missing_probe != nullptr && qa::pack_issue(*missing_probe).has_value() &&
               qa::pack_issue(*missing_probe)->field == "--pack",
           "RF-2.2: a missing pack is rejected in its field");
}

void real_probes(const std::filesystem::path &runtime_path, const std::filesystem::path &core,
                 const std::filesystem::path &non_core, const std::filesystem::path &rom,
                 const std::filesystem::path &fixtures) {
    const auto identified = qa::identify_runtime_binary(runtime_path);
    const auto *runtime = std::get_if<qa::RuntimeBinaryIdentity>(&identified);
    expect(runtime != nullptr, "the Runtime under test is identified");
    if (runtime == nullptr)
        return;

    const auto synthetic = qa::probe_core(*runtime, core, std::nullopt, {});
    const auto *probe = std::get_if<qa::CoreProbe>(&synthetic);
    expect(probe != nullptr && probe->loaded && probe->library_name == "AYTHER Synthetic Core" &&
               probe->extensions == std::vector<std::string>{"aytest", "rom"} &&
               !probe->game_loaded,
           "RF-2.2: the synthetic core is probed by the Runtime");
    if (probe != nullptr) {
        expect(!qa::core_issue(*probe, rom), "RF-1.7: the synthetic core accepts its ROM");
        expect(qa::core_issue(*probe, rom.parent_path() / "game.md") == "core_platform_mismatch",
               "RF-1.7: the synthetic core rejects a Mega Drive ROM");
    }

    const auto with_rom =
        qa::probe_core(*runtime, core, qa::CoreProbeLaunch{rom, {"unused=1"}, std::nullopt}, {});
    const auto *game = std::get_if<qa::CoreProbe>(&with_rom);
    expect(game != nullptr && game->game_loaded == true && game->timing_fps == 60.0 &&
               !qa::core_issue(*game, rom),
           "RF-2.2: the synthetic core loads its ROM and reports 60 fps without running it");

    const auto empty_rom = std::filesystem::temp_directory_path() / "ayther-qa-empty.aytest";
    std::ofstream{empty_rom, std::ios::binary | std::ios::trunc}.close();
    const auto rejected =
        qa::probe_core(*runtime, core, qa::CoreProbeLaunch{empty_rom, {}, std::nullopt}, {});
    const auto *rejection = std::get_if<qa::CoreProbe>(&rejected);
    const auto *rejection_issue = std::get_if<qa::FieldIssue>(&rejected);
    expect((rejection != nullptr && qa::core_issue(*rejection, empty_rom) == "core_rejects_rom") ||
               (rejection_issue != nullptr &&
                *rejection_issue == qa::FieldIssue{"--core", "core_rejects_rom"}),
           "RF-1.7: a ROM the core does not load gives core_rejects_rom");
    std::error_code ignored;
    std::filesystem::remove(empty_rom, ignored);

    const auto library = qa::probe_core(*runtime, non_core, std::nullopt, {});
    const auto *not_core = std::get_if<qa::CoreProbe>(&library);
    const auto *issue = std::get_if<qa::FieldIssue>(&library);
    expect((not_core != nullptr && qa::core_issue(*not_core, rom) == "core_invalid") ||
               (issue != nullptr && *issue == qa::FieldIssue{"--core", "core_invalid"}),
           "RF-2.2: a library that is not libretro is rejected in the --core field");

    const auto missing =
        qa::probe_core(*runtime, core.parent_path() / "missing-core.dll", std::nullopt, {});
    const auto *absent = std::get_if<qa::CoreProbe>(&missing);
    expect(absent != nullptr && qa::core_issue(*absent, rom) == "core_load_failed",
           "RF-2.2: a missing core is rejected");

    real_pack_probes(*runtime, fixtures);
}

} // namespace

int main(int argc, char **argv) {
    probe_output_is_read();
    core_is_compared_with_the_rom();
    pack_probe_output_is_read();
    requested_profile_is_offered();
    if (argc == 6)
        real_probes(argv[1], argv[2], argv[3], argv[4], argv[5]);
    else if (argc != 1)
        std::cerr << "usage: runtime_probe_test [runtime core non-core rom fixtures]\n";
    if (failures != 0 || (argc != 1 && argc != 6))
        return 1;
    std::cout << "cores are probed with their ROM before admission\n";
    return 0;
}
