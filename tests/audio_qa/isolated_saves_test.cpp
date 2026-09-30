#include "isolated_saves.h"
#include "runtime_config.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace qa = ayther::audio_qa;
namespace runtime = ayther::runtime;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write(const std::filesystem::path &path, const std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "fixture_open_failed");
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    require(static_cast<bool>(output), "fixture_write_failed");
}

std::string read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "fixture_read_failed");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void remove_tree(const std::filesystem::path &path) {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

const qa::IsolatedSaves &saves(const qa::IsolatedSavesResult &result) {
    const auto *value = std::get_if<qa::IsolatedSaves>(&result);
    if (value == nullptr) {
        throw std::runtime_error(
            "isolated_saves_were_not_prepared:" +
            std::to_string(static_cast<int>(std::get<qa::IsolatedSavesError>(result))));
    }
    return *value;
}

bool verified(const qa::SaveTreeVerificationResult &result) {
    const auto *value = std::get_if<bool>(&result);
    require(value != nullptr, "user_save_verification_failed");
    return *value;
}

const qa::SaveTreeSnapshot &snapshot(const qa::SaveTreeSnapshotResult &result) {
    const auto *value = std::get_if<qa::SaveTreeSnapshot>(&result);
    if (value == nullptr) {
        throw std::runtime_error(
            "user_save_snapshot_failed:" +
            std::to_string(static_cast<int>(std::get<qa::IsolatedSavesError>(result))));
    }
    return *value;
}

} // namespace

int main() {
    const auto root = std::filesystem::current_path() / "qa-133";
    const auto user_saves = root / "user" / "saves";
    const auto private_saves = root / "run" / "saves";
    const auto preexisting = root / "preexisting";
    remove_tree(root);
    try {
        write(user_saves / "slot-a.srm", "user-save-a");
        write(user_saves / "nested" / "slot-b.srm", "user-save-b");
        std::filesystem::create_directories(private_saves.parent_path());
        require(snapshot(qa::snapshot_save_tree(user_saves)).entries.size() == 3,
                "initial_user_save_snapshot_was_incomplete");

        {
            const auto prepared = qa::prepare_isolated_saves(user_saves, private_saves);
            const auto &isolated = saves(prepared);
            const runtime::RuntimePaths runtime_paths(root / "runtime-data");
            const runtime::RuntimeConfig runtime_config(runtime_paths, isolated.directory());
            require(runtime_config.saves_directory() == private_saves,
                    "runtime_did_not_select_isolated_saves");

            write(runtime_config.saves_directory() / "restored.srm", "restored");
            write(runtime_config.saves_directory() / "played.srm", "played");
            write(runtime_config.saves_directory() / "closed.state", "closed");
            require(verified(isolated.verify_user_saves_unchanged()) &&
                        read(user_saves / "slot-a.srm") == "user-save-a" &&
                        read(user_saves / "nested" / "slot-b.srm") == "user-save-b",
                    "qa_session_changed_user_saves");

            write(user_saves / "slot-a.srm", "unexpected-change");
            require(!verified(isolated.verify_user_saves_unchanged()),
                    "user_save_change_was_not_detected");
            write(user_saves / "slot-a.srm", "user-save-a");
        }
        require(!std::filesystem::exists(private_saves), "isolated_saves_survived_owner");

        const auto overlap = qa::prepare_isolated_saves(user_saves, user_saves / "qa-private");
        require(std::get_if<qa::IsolatedSavesError>(&overlap) != nullptr &&
                    std::get<qa::IsolatedSavesError>(overlap) ==
                        qa::IsolatedSavesError::overlapping_paths &&
                    !std::filesystem::exists(user_saves / "qa-private"),
                "overlapping_save_root_was_accepted");

        write(preexisting / "sentinel", "preserve-me");
        const auto collision = qa::prepare_isolated_saves(user_saves, preexisting);
        require(std::get_if<qa::IsolatedSavesError>(&collision) != nullptr &&
                    std::get<qa::IsolatedSavesError>(collision) ==
                        qa::IsolatedSavesError::staging_path_exists &&
                    read(preexisting / "sentinel") == "preserve-me",
                "preexisting_save_root_was_not_preserved");

        remove_tree(root);
        std::puts("isolated_saves_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(root);
        std::fprintf(stderr, "isolated_saves_test: %s\n", error.what());
        return 1;
    }
}
