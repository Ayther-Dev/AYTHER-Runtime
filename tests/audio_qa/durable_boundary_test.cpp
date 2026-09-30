#include "durable_file.h"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace qa = ayther::audio_qa;
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

std::vector<std::byte> bytes(const std::string &text) {
    const auto *begin = reinterpret_cast<const std::byte *>(text.data());
    return {begin, begin + text.size()};
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "boundary_fixture_open_failed");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

bool no_temporary_files(const std::filesystem::path &directory) {
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().filename().string().starts_with(".ayther-durable-")) {
            return false;
        }
    }
    return true;
}

void require_error(const qa::DurablePublishResult &result, const qa::DurablePublishError expected,
                   const char *const message) {
    const auto *error = std::get_if<qa::DurablePublishError>(&result);
    require(error != nullptr && *error == expected, message);
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-149-boundaries";
    remove_tree(fixture);
    try {
        require(std::filesystem::create_directory(fixture), "boundary_fixture_create_failed");
        const auto target = fixture / "current.toml";
        const auto initial = qa::publish_durable_file(target, bytes("confirmed-v1"));
        require(std::get_if<qa::DurablePublishedFile>(&initial) != nullptr,
                "initial_durable_publication_failed");

        const auto before_flush = qa::publish_durable_file(
            target, bytes("pending-before-flush"),
            qa::DurablePublicationLimits{std::nullopt, qa::DurablePublicationStop::before_flush});
        require_error(before_flush, qa::DurablePublishError::interrupted_before_flush,
                      "before_flush_interruption_was_not_pending");
        require(read_text(target) == "confirmed-v1" && no_temporary_files(fixture),
                "before_flush_interruption_changed_confirmed_data");

        const auto after_flush = qa::publish_durable_file(
            target, bytes("flushed-but-not-published"),
            qa::DurablePublicationLimits{std::nullopt, qa::DurablePublicationStop::after_flush});
        require_error(after_flush, qa::DurablePublishError::interrupted_after_flush,
                      "after_flush_interruption_was_not_pending");
        require(read_text(target) == "confirmed-v1" && no_temporary_files(fixture),
                "after_flush_interruption_changed_confirmed_data");

        const auto confirmed = qa::publish_durable_file(target, bytes("confirmed-v2"));
        require(std::get_if<qa::DurablePublishedFile>(&confirmed) != nullptr &&
                    read_text(target) == "confirmed-v2",
                "published_boundary_was_not_confirmed_durable");

#ifdef _WIN32
        const auto lock =
            CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(lock != INVALID_HANDLE_VALUE, "permission_fixture_lock_failed");
        const auto denied = qa::publish_durable_file(target, bytes("permission-denied"));
        require_error(denied, qa::DurablePublishError::publish_failed,
                      "replacement_permission_failure_was_not_reported");
        require(CloseHandle(lock) != 0, "permission_fixture_unlock_failed");
        require(read_text(target) == "confirmed-v2" && no_temporary_files(fixture),
                "permission_failure_changed_confirmed_data");
#endif

        remove_tree(fixture);
        std::puts("durable_boundary_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "durable_boundary_test: %s\n", error.what());
        return 1;
    }
}
