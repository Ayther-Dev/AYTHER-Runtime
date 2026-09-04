#include "save_state_store.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

int main() {
    using ayther::runtime::SaveStateStatus;
    using ayther::runtime::SaveStateStore;

    const auto root = std::filesystem::temp_directory_path() /
                      "ayther-runtime-save-state-store-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);

    SaveStateStore store;
    const auto missing = store.load(root / "missing.bin");
    if (missing.status != SaveStateStatus::missing || missing.loaded()) {
        return 1;
    }

    const std::vector<std::uint8_t> state{0, 1, 2, 0xff};
    const auto saved = store.save(root, "crc32:ABC/DEF", "CRT premium",
                                  "A0:B1", state, "20260101T010203Z");
    if (!saved || !std::filesystem::exists(saved.path) ||
        std::filesystem::exists(saved.path.string() + ".tmp") ||
        saved.path.parent_path().filename() != "CRT_premium" ||
        saved.path.parent_path().parent_path().filename() !=
            "crc32_ABC_DEF" ||
        saved.path.filename() != "estado-20260101T010203Z-a0_b1.bin") {
        std::filesystem::remove_all(root, error);
        return 2;
    }

    const auto loaded = store.load(saved.path);
    if (!loaded.loaded() || loaded.format_version !=
            ayther::runtime::save_state_format_version ||
        loaded.bytes != state) {
        std::filesystem::remove_all(root, error);
        return 3;
    }

    const std::vector<std::uint8_t> replacement{9, 9, 9};
    const auto duplicate = store.save(
        root, "crc32:ABC/DEF", "CRT premium", "A0:B1", replacement,
        "20260101T010203Z");
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
        legacy.write(reinterpret_cast<const char*>(state.data()),
                     static_cast<std::streamsize>(state.size()));
    }
    const auto legacy = store.load(legacy_path);
    if (!legacy.loaded() || legacy.format_version != 0U ||
        legacy.bytes != state) {
        std::filesystem::remove_all(root, error);
        return 6;
    }

    std::ifstream encoded_input(saved.path, std::ios::binary);
    std::vector<std::uint8_t> encoded{
        std::istreambuf_iterator<char>{encoded_input},
        std::istreambuf_iterator<char>{}};
    encoded_input.close();
    const auto write_bytes = [](const std::filesystem::path& path,
                                const std::vector<std::uint8_t>& bytes) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),
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
    future[8] = 2U;
    future[9] = future[10] = future[11] = 0U;
    const auto future_path = root / "future-v2.bin";
    write_bytes(future_path, future);
    if (store.load(future_path).status !=
        SaveStateStatus::unsupported_version) {
        std::filesystem::remove_all(root, error);
        return 9;
    }

    const ayther::runtime::SaveStateStore disk_full{
        ayther::runtime::SaveStateWriteFault::disk_full};
    const ayther::runtime::SaveStateStore interrupted{
        ayther::runtime::SaveStateWriteFault::before_publish};
    if (disk_full.save(root, "game", "profile", "rev", replacement,
                       "20260101T020000Z").status !=
            SaveStateStatus::io_error ||
        interrupted.save(root, "game", "profile", "rev", replacement,
                         "20260101T030000Z").status !=
            SaveStateStatus::io_error ||
        store.load(saved.path).bytes != state) {
        std::filesystem::remove_all(root, error);
        return 10;
    }

    std::filesystem::remove_all(root, error);
    return error ? 11 : 0;
}
