#include "save_state_store.h"

#include <filesystem>
#include <fstream>
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
    if (!loaded.loaded() || loaded.bytes != state) {
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

    std::filesystem::remove_all(root, error);
    return error ? 6 : 0;
}
