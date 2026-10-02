#include "content_hash.h"
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

std::vector<std::byte> bytes(const std::string &text) {
    const auto *begin = reinterpret_cast<const std::byte *>(text.data());
    return {begin, begin + text.size()};
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "durable_file_open_failed");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void remove_tree(const std::filesystem::path &path) {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

bool no_temporary_files(const std::filesystem::path &directory) {
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().filename().string().starts_with(".ayther-durable-")) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-140-durable-file";
    remove_tree(fixture);
    try {
        require(std::filesystem::create_directory(fixture), "durable_fixture_create_failed");
        const auto target = fixture / "fragment.aqf";
        const auto first = bytes("durable-fragment-v1");
        const auto first_result = qa::publish_durable_file(target, first);
        const auto *published = std::get_if<qa::DurablePublishedFile>(&first_result);
        require(published != nullptr && published->path() == target &&
                    published->identity() == qa::identify_content(first) &&
                    read_text(target) == "durable-fragment-v1" && no_temporary_files(fixture),
                "durable_publish_did_not_confirm_written_bytes");

#ifdef _WIN32
        const auto lock =
            CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(lock != INVALID_HANDLE_VALUE, "previous_fragment_lock_failed");
        const auto replacement = bytes("durable-fragment-v2");
        const auto failed = qa::publish_durable_file(target, replacement);
        const bool failed_at_publish =
            std::get_if<qa::DurablePublishError>(&failed) != nullptr &&
            std::get<qa::DurablePublishError>(failed) == qa::DurablePublishError::publish_failed;
        require(CloseHandle(lock) != 0, "previous_fragment_unlock_failed");
        require(failed_at_publish && read_text(target) == "durable-fragment-v1" &&
                    no_temporary_files(fixture),
                "failed_publish_changed_previous_fragment");
#endif

        remove_tree(fixture);
        std::puts("durable_file_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "durable_file_test: %s\n", error.what());
        return 1;
    }
}
