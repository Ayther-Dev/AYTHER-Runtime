#include "isolated_runtime_data.h"
#include "player_config.h"
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

const qa::IsolatedRuntimeData &runtime_data(const qa::IsolatedRuntimeDataResult &result) {
    const auto *value = std::get_if<qa::IsolatedRuntimeData>(&result);
    if (value == nullptr)
        throw std::runtime_error(
            "isolated_runtime_data_was_not_prepared:" +
            std::to_string(static_cast<int>(std::get<qa::IsolatedSavesError>(result))));
    return *value;
}

bool verified(const qa::SaveTreeVerificationResult &result) {
    const auto *value = std::get_if<bool>(&result);
    require(value != nullptr, "user_runtime_data_verification_failed");
    return *value;
}

} // namespace

int main() {
    const auto root = std::filesystem::current_path() / "qa-134";
    const auto user_data = root / "user-runtime";
    const auto private_data = root / "qa-runtime";
    remove_tree(root);
    try {
        const auto user_config = ayther::player_config_path(user_data, "game", "pack");
        write(user_config, "format_version = 1\nprofile = \"user\"\nshaders = true\n");
        write(user_data / "diagnostico.md", "user-diagnostic");
        const auto initial_snapshot = qa::snapshot_save_tree(user_data);
        if (const auto *error = std::get_if<qa::IsolatedSavesError>(&initial_snapshot))
            throw std::runtime_error("initial_snapshot_failed:" +
                                     std::to_string(static_cast<int>(*error)));

        {
            const auto prepared = qa::prepare_isolated_runtime_data(user_data, private_data);
            const auto &isolated = runtime_data(prepared);
            const runtime::RuntimePaths qa_paths(isolated.directory());
            const runtime::RuntimeConfig qa_config(qa_paths);
            require(qa_paths.configuration_directory() == private_data &&
                        qa_paths.captures_directory().parent_path() == private_data &&
                        qa_paths.diagnostics_file().parent_path() == private_data &&
                        qa_config.saves_directory().parent_path() == private_data,
                    "qa_runtime_paths_escaped_private_root");

            ayther::PlayerConfig changed;
            changed.profile = "qa-only";
            changed.output = "lcd";
            changed.hd_on = false;
            const auto qa_config_file =
                ayther::player_config_path(qa_paths.configuration_directory(), "game", "pack");
            require(ayther::player_config_save(qa_config_file, changed),
                    "qa_player_config_write_failed");
            write(qa_paths.diagnostics_file(), "qa-diagnostic");
            write(qa_paths.captures_directory() / "capture.txt", "qa-capture");

            require(verified(isolated.verify_user_data_unchanged()) &&
                        read(user_config).find("profile = \"user\"") != std::string::npos &&
                        read(user_data / "diagnostico.md") == "user-diagnostic",
                    "qa_changed_normal_runtime_configuration");
            require(ayther::player_config_load(qa_config_file).profile == "qa-only",
                    "qa_configuration_was_not_written_to_private_root");

            write(user_data / "diagnostico.md", "unexpected-change");
            require(!verified(isolated.verify_user_data_unchanged()),
                    "user_runtime_data_change_was_not_detected");
            write(user_data / "diagnostico.md", "user-diagnostic");
        }
        require(!std::filesystem::exists(private_data), "private_runtime_data_survived_owner");

        const runtime::RuntimePaths normal_paths(user_data);
        require(normal_paths.configuration_directory() == user_data &&
                    ayther::player_config_load(user_config).profile == "user",
                "normal_runtime_configuration_changed_after_qa");

        remove_tree(root);
        std::puts("isolated_runtime_data_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(root);
        std::fprintf(stderr, "isolated_runtime_data_test: %s\n", error.what());
        return 1;
    }
}
