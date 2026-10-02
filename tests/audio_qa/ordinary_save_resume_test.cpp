#include "isolated_saves.h"
#include "save_state_store.h"

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace runtime = ayther::runtime;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void remove_tree(const std::filesystem::path &path) {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

const qa::IsolatedSaves &saves(const qa::IsolatedSavesResult &result) {
    const auto *value = std::get_if<qa::IsolatedSaves>(&result);
    if (value == nullptr)
        throw std::runtime_error(
            "isolated_saves_were_not_prepared:" +
            std::to_string(static_cast<int>(std::get<qa::IsolatedSavesError>(result))));
    return *value;
}

const qa::SaveTreeSnapshot &snapshot(const qa::SaveTreeSnapshotResult &result) {
    const auto *value = std::get_if<qa::SaveTreeSnapshot>(&result);
    require(value != nullptr, "save_tree_snapshot_failed");
    return *value;
}

bool verified(const qa::SaveTreeVerificationResult &result) {
    const auto *value = std::get_if<bool>(&result);
    require(value != nullptr, "save_tree_verification_failed");
    return *value;
}

} // namespace

int main() {
    const auto root = std::filesystem::current_path() / "qa-179";
    const auto user_saves = root / "user" / "saves";
    const auto private_saves = root / "run" / "saves";
    const std::vector<std::uint8_t> ordinary_state{0x10, 0x20, 0x30, 0x40};
    const std::vector<std::uint8_t> qa_state{0xaa, 0xbb, 0xcc};
    remove_tree(root);

    try {
        runtime::SaveStateStore ordinary_store;
        const auto ordinary_saved = ordinary_store.save(
            user_saves, "golden-axe", "original", "665d7df9", ordinary_state, "20260929T120000Z");
        require(static_cast<bool>(ordinary_saved), "ordinary_save_failed");

        const runtime::SaveStateStore resumed_store;
        const auto first_resume = resumed_store.load(ordinary_saved.path);
        require(first_resume.loaded() &&
                    first_resume.format_version == runtime::save_state_format_version &&
                    first_resume.bytes == ordinary_state,
                "ordinary_resume_before_qa_failed");
        const auto before_qa = snapshot(qa::snapshot_save_tree(user_saves));
        std::filesystem::create_directories(private_saves.parent_path());

        {
            const auto prepared = qa::prepare_isolated_saves(user_saves, private_saves);
            const auto &isolated = saves(prepared);
            const runtime::SaveStateStore qa_store;
            const auto qa_saved = qa_store.save(isolated.directory(), "golden-axe", "original",
                                                "665d7df9", qa_state, "20260929T120100Z");
            require(static_cast<bool>(qa_saved), "qa_private_save_failed");
            const auto qa_resume = qa_store.load(qa_saved.path);
            require(qa_resume.loaded() && qa_resume.bytes == qa_state, "qa_private_resume_failed");
            require(verified(isolated.verify_user_saves_unchanged()),
                    "qa_session_changed_ordinary_saves");
        }

        require(!std::filesystem::exists(private_saves), "qa_private_saves_survived_session");
        require(snapshot(qa::snapshot_save_tree(user_saves)) == before_qa,
                "ordinary_save_tree_changed_after_qa");

        const runtime::SaveStateStore restarted_store;
        const auto second_resume = restarted_store.load(ordinary_saved.path);
        require(second_resume.loaded() &&
                    second_resume.format_version == runtime::save_state_format_version &&
                    second_resume.bytes == ordinary_state,
                "ordinary_resume_after_qa_failed");

        remove_tree(root);
        std::puts("ordinary_save_resume_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(root);
        std::fprintf(stderr, "ordinary_save_resume_test: %s\n", error.what());
        return 1;
    }
}
