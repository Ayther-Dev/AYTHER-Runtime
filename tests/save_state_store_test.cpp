#include "save_state_store.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main() {
    using ayther::runtime::SaveStateStatus;
    using ayther::runtime::SaveStateStore;

    const auto root =
        std::filesystem::temp_directory_path() / "ayther-runtime-save-state-store-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);

    SaveStateStore store;
    const auto missing = store.load(root / "missing.bin");
    if (missing.status != SaveStateStatus::missing || missing.loaded()) {
        return 1;
    }

    const std::vector<std::uint8_t> state{0, 1, 2, 0xff};
    const auto saved =
        store.save(root, "crc32:ABC/DEF", "CRT premium", "A0:B1", state, "20260101T010203Z");
    if (!saved || !std::filesystem::exists(saved.path) ||
        std::filesystem::exists(saved.path.string() + ".tmp") ||
        saved.path.parent_path().filename() != "CRT_premium" ||
        saved.path.parent_path().parent_path().filename() != "crc32_ABC_DEF" ||
        saved.path.filename() != "estado-20260101T010203Z-a0_b1.bin") {
        std::filesystem::remove_all(root, error);
        return 2;
    }

    const auto loaded = store.load(saved.path);
    if (!loaded.loaded() || loaded.format_version != ayther::runtime::save_state_format_version ||
        loaded.bytes != state || loaded.generation != ayther::runtime::SaveStateGeneration{}) {
        std::filesystem::remove_all(root, error);
        return 3;
    }

    const std::vector<std::uint8_t> replacement{9, 9, 9};
    const auto duplicate =
        store.save(root, "crc32:ABC/DEF", "CRT premium", "A0:B1", replacement, "20260101T010203Z");
    if (duplicate.status != SaveStateStatus::target_exists ||
        store.load(saved.path).bytes != state) {
        std::filesystem::remove_all(root, error);
        return 4;
    }

    const auto empty_path = root / "empty.bin";
    std::ofstream(empty_path, std::ios::binary).close();
    if (store.load(empty_path).status != SaveStateStatus::invalid) {
        std::filesystem::remove_all(root, error);
        return 5;
    }

    const auto legacy_path = root / "legacy-v0.bin";
    {
        std::ofstream legacy(legacy_path, std::ios::binary | std::ios::trunc);
        legacy.write(reinterpret_cast<const char *>(state.data()),
                     static_cast<std::streamsize>(state.size()));
    }
    const auto legacy = store.load(legacy_path);
    if (!legacy.loaded() || legacy.format_version != 0U || legacy.bytes != state ||
        legacy.generation != ayther::runtime::SaveStateGeneration{"gpgx", 1U}) {
        std::filesystem::remove_all(root, error);
        return 6;
    }

    std::ifstream encoded_input(saved.path, std::ios::binary);
    std::vector<std::uint8_t> encoded{std::istreambuf_iterator<char>{encoded_input},
                                      std::istreambuf_iterator<char>{}};
    encoded_input.close();
    const auto write_bytes = [](const std::filesystem::path &path,
                                const std::vector<std::uint8_t> &bytes) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    };

    auto truncated = encoded;
    truncated.resize(12U);
    const auto truncated_path = root / "truncated-v1.bin";
    write_bytes(truncated_path, truncated);
    if (store.load(truncated_path).status != SaveStateStatus::invalid) {
        std::filesystem::remove_all(root, error);
        return 7;
    }

    auto corrupt = encoded;
    corrupt.back() ^= 0xffU;
    const auto corrupt_path = root / "corrupt-v1.bin";
    write_bytes(corrupt_path, corrupt);
    if (store.load(corrupt_path).status != SaveStateStatus::invalid) {
        std::filesystem::remove_all(root, error);
        return 8;
    }

    auto future = encoded;
    future[8] = 3U;
    future[9] = future[10] = future[11] = 0U;
    const auto future_path = root / "future-v3.bin";
    write_bytes(future_path, future);
    if (store.load(future_path).status != SaveStateStatus::unsupported_version) {
        std::filesystem::remove_all(root, error);
        return 9;
    }

    // Spec 008 (RF-4.1, TP-304.4): version 2 carries the generation.
    const ayther::runtime::SaveStateGeneration tessera{"tessera", 1U};
    const auto native =
        store.save(root, "game", "profile", "rev", state, "20260101T040000Z", tessera);
    const auto native_loaded = store.load(native.path);
    if (!native || !native_loaded.loaded() || native_loaded.generation != tessera ||
        native_loaded.bytes != state) {
        std::filesystem::remove_all(root, error);
        return 12;
    }

    // A version 1 file (no generation fields) was written by gpgx.
    std::vector<std::uint8_t> v1{'A', 'Y', 'T', 'H', 'S', 'T', 'A', 'T', 1, 0, 0, 0};
    for (int index = 0; index < 8; ++index) {
        v1.push_back(index == 0 ? static_cast<std::uint8_t>(state.size()) : 0U);
    }
    std::uint32_t hash = 2166136261U;
    for (const auto byte : state) {
        hash ^= byte;
        hash *= 16777619U;
    }
    for (int index = 0; index < 4; ++index) {
        v1.push_back(static_cast<std::uint8_t>(hash >> (index * 8)));
    }
    v1.insert(v1.end(), state.begin(), state.end());
    const auto v1_path = root / "legacy-v1.bin";
    write_bytes(v1_path, v1);
    const auto v1_loaded = store.load(v1_path);
    if (!v1_loaded.loaded() || v1_loaded.format_version != 1U ||
        v1_loaded.generation != ayther::runtime::SaveStateGeneration{"gpgx", 1U} ||
        v1_loaded.bytes != state) {
        std::filesystem::remove_all(root, error);
        return 13;
    }

    // The emulator id is bounded and printable, on write and on read.
    if (store.save(root, "game", "profile", "rev", state, "20260101T050000Z", {"", 1U}).status !=
            SaveStateStatus::invalid ||
        store.save(root, "game", "profile", "rev", state, "20260101T050001Z",
                   {std::string(33, 'x'), 1U})
                .status != SaveStateStatus::invalid) {
        std::filesystem::remove_all(root, error);
        return 14;
    }
    std::ifstream native_input(native.path, std::ios::binary);
    std::vector<std::uint8_t> native_bytes{std::istreambuf_iterator<char>{native_input},
                                           std::istreambuf_iterator<char>{}};
    native_input.close();
    auto bad_id = native_bytes;
    bad_id[14] = 0x01U; // first byte of "tessera"
    const auto bad_id_path = root / "bad-id-v2.bin";
    write_bytes(bad_id_path, bad_id);
    auto long_id = native_bytes;
    long_id[12] = 200U; // id length beyond 32
    const auto long_id_path = root / "long-id-v2.bin";
    write_bytes(long_id_path, long_id);
    if (store.load(bad_id_path).status != SaveStateStatus::invalid ||
        store.load(long_id_path).status != SaveStateStatus::invalid) {
        std::filesystem::remove_all(root, error);
        return 15;
    }

    const ayther::runtime::SaveStateStore disk_full{
        ayther::runtime::SaveStateWriteFault::disk_full};
    const ayther::runtime::SaveStateStore interrupted{
        ayther::runtime::SaveStateWriteFault::before_publish};
    if (disk_full.save(root, "game", "profile", "rev", replacement, "20260101T020000Z").status !=
            SaveStateStatus::io_error ||
        interrupted.save(root, "game", "profile", "rev", replacement, "20260101T030000Z").status !=
            SaveStateStatus::io_error ||
        store.load(saved.path).bytes != state) {
        std::filesystem::remove_all(root, error);
        return 10;
    }

    std::filesystem::remove_all(root, error);
    return error ? 11 : 0;
}
