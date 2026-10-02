#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace runtime {

inline constexpr std::uint32_t music_state_bundle_version = 1;

struct MusicStateBundle {
    std::vector<std::uint8_t> game_state;
    std::vector<std::uint8_t> hd_state;
    std::string pack_revision;
    friend bool operator==(const MusicStateBundle &, const MusicStateBundle &) = default;
};

enum class MusicStateBundleStatus {
    saved,
    loaded,
    missing,
    invalid,
    unsupported_version,
    io_error
};
enum class MusicStateBundleFault { none, disk_full, before_publish };

struct MusicStateBundleResult {
    MusicStateBundleStatus status{MusicStateBundleStatus::invalid};
    MusicStateBundle bundle;
};

class MusicStateBundleStore {
  public:
    explicit MusicStateBundleStore(MusicStateBundleFault fault = MusicStateBundleFault::none)
        : fault_(fault) {}

    [[nodiscard]] MusicStateBundleResult load(const std::filesystem::path &path) const {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return {std::filesystem::exists(path) ? MusicStateBundleStatus::io_error
                                                  : MusicStateBundleStatus::missing,
                    {}};
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{input}, {}};
        return decode(bytes);
    }

    [[nodiscard]] MusicStateBundleResult save(const std::filesystem::path &path,
                                              const MusicStateBundle &bundle) const {
        if (fault_ == MusicStateBundleFault::disk_full || bundle.game_state.empty() ||
            bundle.hd_state.empty() || bundle.pack_revision.empty())
            return {fault_ == MusicStateBundleFault::disk_full ? MusicStateBundleStatus::io_error
                                                               : MusicStateBundleStatus::invalid,
                    {}};
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
            return {MusicStateBundleStatus::io_error, {}};
        const auto temporary = std::filesystem::path{path.string() + ".tmp"};
        const auto backup = std::filesystem::path{path.string() + ".bak"};
        const auto bytes = encode(bundle);
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            output.flush();
            if (!output) {
                std::filesystem::remove(temporary, error);
                return {MusicStateBundleStatus::io_error, {}};
            }
        }
        if (fault_ == MusicStateBundleFault::before_publish ||
            decode_file(temporary).status != MusicStateBundleStatus::loaded) {
            std::filesystem::remove(temporary, error);
            return {MusicStateBundleStatus::io_error, {}};
        }
        const bool had_previous = std::filesystem::exists(path);
        std::filesystem::remove(backup, error);
        error.clear();
        if (had_previous) {
            std::filesystem::rename(path, backup, error);
            if (error) {
                std::filesystem::remove(temporary, error);
                return {MusicStateBundleStatus::io_error, {}};
            }
        }
        std::filesystem::rename(temporary, path, error);
        if (error) {
            if (had_previous) {
                std::error_code rollback_error;
                std::filesystem::rename(backup, path, rollback_error);
            }
            std::filesystem::remove(temporary, error);
            return {MusicStateBundleStatus::io_error, {}};
        }
        std::filesystem::remove(backup, error);
        return {MusicStateBundleStatus::saved, bundle};
    }

  private:
    static constexpr std::array<std::uint8_t, 8> magic{'A', 'Y', 'M', 'U', 'S', 'I', 'C', 'S'};

    template <class T> static void append(std::vector<std::uint8_t> &out, T value) {
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            out.push_back(static_cast<std::uint8_t>(value & 0xffU));
            value >>= 8U;
        }
    }
    template <class T> static T read(std::span<const std::uint8_t> bytes, std::size_t offset) {
        T value{};
        for (std::size_t i = 0; i < sizeof(T); ++i)
            value |= static_cast<T>(bytes[offset + i]) << (i * 8U);
        return value;
    }
    static std::uint32_t checksum(std::span<const std::uint8_t> bytes) {
        std::uint32_t value = 2166136261U;
        for (auto byte : bytes) {
            value ^= byte;
            value *= 16777619U;
        }
        return value;
    }
    static std::vector<std::uint8_t> encode(const MusicStateBundle &bundle) {
        std::vector<std::uint8_t> payload;
        payload.insert(payload.end(), bundle.game_state.begin(), bundle.game_state.end());
        payload.insert(payload.end(), bundle.hd_state.begin(), bundle.hd_state.end());
        payload.insert(payload.end(), bundle.pack_revision.begin(), bundle.pack_revision.end());
        std::vector<std::uint8_t> bytes{magic.begin(), magic.end()};
        append(bytes, music_state_bundle_version);
        append(bytes, static_cast<std::uint64_t>(bundle.game_state.size()));
        append(bytes, static_cast<std::uint64_t>(bundle.hd_state.size()));
        append(bytes, static_cast<std::uint32_t>(bundle.pack_revision.size()));
        append(bytes, checksum(payload));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        return bytes;
    }
    static MusicStateBundleResult decode_file(const std::filesystem::path &path) {
        std::ifstream input(path, std::ios::binary);
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{input}, {}};
        return decode(bytes);
    }
    static MusicStateBundleResult decode(const std::vector<std::uint8_t> &bytes) {
        constexpr std::size_t header = 8 + 4 + 8 + 8 + 4 + 4;
        if (bytes.size() < header || !std::equal(magic.begin(), magic.end(), bytes.begin()))
            return {MusicStateBundleStatus::invalid, {}};
        const std::span<const std::uint8_t> view{bytes};
        const auto version = read<std::uint32_t>(view, 8);
        if (version != music_state_bundle_version)
            return {MusicStateBundleStatus::unsupported_version, {}};
        const auto game_size = read<std::uint64_t>(view, 12);
        const auto hd_size = read<std::uint64_t>(view, 20);
        const auto pack_size = read<std::uint32_t>(view, 28);
        const auto expected = read<std::uint32_t>(view, 32);
        if (game_size + hd_size + pack_size != bytes.size() - header)
            return {MusicStateBundleStatus::invalid, {}};
        const auto payload = view.subspan(header);
        if (checksum(payload) != expected)
            return {MusicStateBundleStatus::invalid, {}};
        MusicStateBundle bundle;
        auto cursor = payload.begin();
        bundle.game_state.assign(cursor, cursor + static_cast<std::ptrdiff_t>(game_size));
        cursor += static_cast<std::ptrdiff_t>(game_size);
        bundle.hd_state.assign(cursor, cursor + static_cast<std::ptrdiff_t>(hd_size));
        cursor += static_cast<std::ptrdiff_t>(hd_size);
        bundle.pack_revision.assign(cursor, cursor + static_cast<std::ptrdiff_t>(pack_size));
        return {MusicStateBundleStatus::loaded, std::move(bundle)};
    }

    MusicStateBundleFault fault_;
};

} // namespace runtime
