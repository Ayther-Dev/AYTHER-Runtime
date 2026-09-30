#include "content_hash.h"
#include "pinned_take.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::span<const std::byte> bytes(const std::string_view text) {
    return {reinterpret_cast<const std::byte *>(text.data()), text.size()};
}

void write(const std::filesystem::path &path, const std::string_view content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "fixture_open_failed");
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    require(static_cast<bool>(output), "fixture_write_failed");
}

const qa::PinnedTake &take(const qa::PinnedTakeResult &result, const char *const message) {
    const auto *value = std::get_if<qa::PinnedTake>(&result);
    require(value != nullptr, message);
    return *value;
}

} // namespace

int main() {
    const auto root = std::filesystem::current_path();
    const auto path = root / "qa-130-take.ayr";
    const auto replacement = root / "qa-130-replacement.ayr";
    (void)std::filesystem::remove(path);
    (void)std::filesystem::remove(replacement);
    try {
        constexpr std::array<std::uint8_t, 32> abc_sha{
            0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
            0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
            0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
        const auto abc = qa::identify_content(bytes("abc"));
        require(abc.sha256 == abc_sha && abc.byte_size == 3,
                "content_identity_known_vector_failed");

        constexpr std::string_view original = "ARP1-original-take-bytes";
        constexpr std::string_view changed = "ARP1-replacement-take-bytes";
        write(path, original);
        const auto expected = qa::identify_content(bytes(original));
        const auto opened = qa::open_pinned_take(path, expected);
        const auto &pinned = take(opened, "original_take_was_not_pinned");

        write(replacement, changed);
        std::filesystem::remove(path);
        std::filesystem::rename(replacement, path);
        require(
            pinned.bytes().size() == original.size() &&
                std::equal(pinned.bytes().begin(), pinned.bytes().end(), bytes(original).begin()) &&
                pinned.identity() == expected,
            "external_substitution_changed_pinned_bytes");

        const auto reopened = qa::open_pinned_take(path);
        const auto &new_take = take(reopened, "replacement_take_was_not_read");
        require(new_take.identity() != pinned.identity() &&
                    std::equal(new_take.bytes().begin(), new_take.bytes().end(),
                               bytes(changed).begin()),
                "replacement_was_not_observable_as_distinct_content");

        const auto mismatch = qa::open_pinned_take(path, expected);
        require(std::get_if<qa::PinnedTakeError>(&mismatch) != nullptr &&
                    std::get<qa::PinnedTakeError>(mismatch) ==
                        qa::PinnedTakeError::identity_mismatch,
                "unexpected_take_content_was_not_rejected");

        (void)std::filesystem::remove(path);
        std::puts("pinned_take_test: passed");
        return 0;
    } catch (const std::exception &error) {
        (void)std::filesystem::remove(path);
        (void)std::filesystem::remove(replacement);
        std::fprintf(stderr, "pinned_take_test: %s\n", error.what());
        return 1;
    }
}
