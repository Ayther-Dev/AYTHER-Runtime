#include "music_state_bundle_store.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

int main() {
    namespace fs = std::filesystem;
    using runtime::MusicStateBundle;
    using runtime::MusicStateBundleFault;
    using runtime::MusicStateBundleStatus;
    using runtime::MusicStateBundleStore;

    const auto root = fs::temp_directory_path() / "ayther-music-state-bundle-test";
    const auto state_path = root / "slot.aystate";
    std::error_code error;
    fs::remove_all(root, error);

    const MusicStateBundle first{{1, 2, 3}, {4, 5}, "pack-r1"};
    MusicStateBundleStore store;
    if (store.save(state_path, first).status != MusicStateBundleStatus::saved ||
        store.load(state_path).bundle != first)
        return 1;

    const MusicStateBundle replacement{{9}, {8, 7, 6}, "pack-r2"};
    for (const auto fault :
         {MusicStateBundleFault::disk_full, MusicStateBundleFault::before_publish}) {
        MusicStateBundleStore failing{fault};
        if (failing.save(state_path, replacement).status != MusicStateBundleStatus::io_error ||
            store.load(state_path).bundle != first || fs::exists(state_path.string() + ".tmp"))
            return 2;
    }

    if (store.save(state_path, replacement).status != MusicStateBundleStatus::saved ||
        store.load(state_path).bundle != replacement)
        return 3;

    std::ifstream input(state_path, std::ios::binary);
    std::vector<unsigned char> future{std::istreambuf_iterator<char>{input}, {}};
    input.close();
    future[8] = 2;
    const auto future_path = root / "future.aystate";
    std::ofstream output(future_path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(future.data()),
                 static_cast<std::streamsize>(future.size()));
    output.close();
    const auto before = future;
    if (store.load(future_path).status != MusicStateBundleStatus::unsupported_version) {
        return 4;
    }
    std::ifstream after_input(future_path, std::ios::binary);
    const std::vector<unsigned char> after{std::istreambuf_iterator<char>{after_input}, {}};
    after_input.close();
    if (after != before)
        return 5;

    fs::remove_all(root, error);
    return error ? 6 : 0;
}
