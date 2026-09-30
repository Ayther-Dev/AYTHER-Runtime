#include "pinned_take.h"

#include "content_hash.h"

#include <fstream>
#include <limits>
#include <utility>

namespace ayther::audio_qa {

PinnedTake::PinnedTake(std::filesystem::path source_path, std::vector<std::byte> bytes,
                       const ContentIdentity identity)
    : source_path_(std::move(source_path)), bytes_(std::move(bytes)), identity_(identity) {}

const std::filesystem::path &PinnedTake::source_path() const noexcept { return source_path_; }

std::span<const std::byte> PinnedTake::bytes() const noexcept { return bytes_; }

const ContentIdentity &PinnedTake::identity() const noexcept { return identity_; }

PinnedTakeResult open_pinned_take(const std::filesystem::path &path,
                                  const std::optional<ContentIdentity> expected) noexcept {
    try {
        if (path.empty()) {
            return PinnedTakeError::invalid_path;
        }
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) {
            return PinnedTakeError::io_error;
        }
        if (size == 0) {
            return PinnedTakeError::empty_input;
        }
        if (size > max_pinned_take_bytes ||
            size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
            return PinnedTakeError::input_too_large;
        }

        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return PinnedTakeError::io_error;
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        input.read(reinterpret_cast<char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            return PinnedTakeError::io_error;
        }
        char unexpected{};
        if (input.read(&unexpected, 1)) {
            return PinnedTakeError::io_error;
        }

        const auto identity = identify_content(bytes);
        if (expected.has_value() && *expected != identity) {
            return PinnedTakeError::identity_mismatch;
        }
        return PinnedTake{path, std::move(bytes), identity};
    } catch (...) {
        return PinnedTakeError::io_error;
    }
}

} // namespace ayther::audio_qa
