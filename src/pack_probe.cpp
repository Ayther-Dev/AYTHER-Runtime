#include "pack_probe.h"

#include "status_emitter.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <limits>
#include <system_error>
#include <utility>

// Runtime owns its image decoder for the probe. Keep stb's definitions local to this
// translation unit so they cannot collide with implementation details from Engine.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include <stb_image.h>

namespace ayther::runtime {
namespace {

void add_asset(PackCatalog &catalog, std::string asset) {
    if (!asset.empty() &&
        std::find(catalog.assets.begin(), catalog.assets.end(), asset) == catalog.assets.end())
        catalog.assets.push_back(std::move(asset));
}

// The Engine reads a hexadecimal u64 with or without `0x`.
bool hexadecimal_u64(std::string_view text) {
    if (text.starts_with("0x") || text.starts_with("0X"))
        text.remove_prefix(2);
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

// Every pose needs its hashes and its asset: the Engine drops the whole catalog otherwise.
bool read_poses(std::string_view text, PackCatalog &catalog) {
    const auto table = toml::parse(text);
    const auto *poses = table["pose"].as_array();
    if (poses == nullptr)
        return !table.contains("pose");
    for (const auto &node : *poses) {
        const auto *pose = node.as_table();
        if (pose == nullptr || !(*pose)["hashes"].is_array())
            return false;
        const auto asset = (*pose)["asset"].value<std::string>();
        if (!asset)
            return false;
        ++catalog.poses;
        add_asset(catalog, *asset);
        if (const auto mask = (*pose)["mask"].value<std::string>())
            add_asset(catalog, *mask);
        if (const auto *variants = (*pose)["variant"].as_array()) {
            for (const auto &variant_node : *variants) {
                const auto *variant = variant_node.as_table();
                const auto variant_asset =
                    variant == nullptr ? std::nullopt : (*variant)["asset"].value<std::string>();
                if (!variant_asset)
                    return false;
                add_asset(catalog, *variant_asset);
            }
        }
    }
    return true;
}

// The Engine skips events without a hexadecimal signature or a textual asset; the probe
// reports them instead of letting them disappear.
bool read_audio_events(std::string_view text, PackCatalog &catalog) {
    const auto table = toml::parse(text);
    const auto *events = table["event"].as_array();
    if (events == nullptr)
        return false;
    for (const auto &node : *events) {
        const auto *event = node.as_table();
        if (event == nullptr)
            return false;
        const auto signature = (*event)["signature"].value<std::string>();
        const auto asset = (*event)["asset"].value<std::string>();
        if (!signature || !hexadecimal_u64(*signature) || !asset)
            return false;
        ++catalog.audio_events;
        add_asset(catalog, *asset);
    }
    return true;
}

std::uint32_t read_u32_le(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::uint16_t read_u16_le(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1U] << 8U));
}

bool starts_with(std::span<const std::uint8_t> bytes, std::string_view magic) noexcept {
    return bytes.size() >= magic.size() &&
           std::memcmp(bytes.data(), magic.data(), magic.size()) == 0;
}

// RIFF/WAVE: every chunk inside the file, a PCM-like `fmt ` and a `data` chunk.
bool wave_decodes(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() < 12U || read_u32_le(bytes, 4U) > bytes.size() - 8U)
        return false;
    bool format{};
    bool data{};
    std::size_t offset = 12U;
    while (bytes.size() - offset >= 8U) {
        const auto size = read_u32_le(bytes, offset + 4U);
        if (size > bytes.size() - offset - 8U)
            return false;
        const auto chunk = bytes.subspan(offset + 8U, size);
        if (std::memcmp(bytes.data() + offset, "fmt ", 4U) == 0) {
            if (size < 16U)
                return false;
            const auto channels = read_u16_le(chunk, 2U);
            const auto rate = read_u32_le(chunk, 4U);
            const auto bits = read_u16_le(chunk, 14U);
            format = channels != 0U && rate != 0U && bits != 0U && bits % 8U == 0U;
            if (!format)
                return false;
        } else if (std::memcmp(bytes.data() + offset, "data", 4U) == 0) {
            data = true;
        }
        offset += 8U + size + (size % 2U);
        if (offset > bytes.size())
            break;
    }
    return format && data;
}

void append_string_array(std::string &json, const std::vector<std::string> &values) {
    json.push_back('[');
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U)
            json.push_back(',');
        append_json_string(json, values[index]);
    }
    json.push_back(']');
}

void append_field(std::string &json, std::string_view name, std::string_view value) {
    json.push_back(',');
    append_json_string(json, name);
    json.push_back(':');
    append_json_string(json, value);
}

} // namespace

std::optional<PackCatalog> read_pack_catalog(std::string_view poses,
                                             std::string_view audio_events) {
    PackCatalog catalog;
    try {
        if (!poses.empty() && !read_poses(poses, catalog))
            return std::nullopt;
        if (!audio_events.empty() && !read_audio_events(audio_events, catalog))
            return std::nullopt;
    } catch (const toml::parse_error &) {
        return std::nullopt;
    }
    return catalog;
}

bool asset_decodes(std::span<const std::uint8_t> bytes) noexcept {
    constexpr std::string_view png{"\x89PNG\r\n\x1a\n", 8};
    constexpr std::string_view jpeg{"\xff\xd8\xff", 3};
    if (starts_with(bytes, png) || starts_with(bytes, jpeg)) {
        if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return false;
        int width{};
        int height{};
        int channels{};
        auto *pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width,
                                             &height, &channels, 0);
        if (pixels == nullptr)
            return false;
        stbi_image_free(pixels);
        return width > 0 && height > 0;
    }
    if (starts_with(bytes, "RIFF") && bytes.size() >= 12U &&
        std::memcmp(bytes.data() + 8U, "WAVE", 4U) == 0)
        return wave_decodes(bytes);
    return true;
}

std::string pack_probe_reason(const PackProbeReport &report) {
    if (!report.errors.empty())
        return "pack_invalid";
    if (report.signed_pack && report.trust == "unverified")
        return "pack_trust_unverified";
    if (report.trust == "untrusted")
        return "pack_untrusted";
    if (!report.opened)
        return "pack_open_failed";
    if (!report.catalog)
        return "pack_catalog_invalid";
    if (!report.unreadable_assets.empty())
        return "pack_assets_unreadable";
    return {};
}

std::string format_pack_probe_line(const PackProbeReport &report) {
    std::string json{R"({"schema":"1.0","opened":)"};
    json += report.opened ? "true" : "false";
    append_field(json, "signature", report.signature);
    append_field(json, "trust", report.trust);
    json += R"(,"catalog":)";
    if (report.catalog) {
        json += R"({"poses":)";
        json += std::to_string(report.catalog->poses);
        json += R"(,"audio_events":)";
        json += std::to_string(report.catalog->audio_events);
        json.push_back('}');
    } else {
        json += "null";
    }
    json += R"(,"unreadable_assets":)";
    append_string_array(json, report.unreadable_assets);
    json += R"(,"profiles":)";
    append_string_array(json, report.profiles);
    append_field(json, "game_id", report.game_id);
    json += R"(,"errors":)";
    append_string_array(json, report.errors);
    if (const auto reason = pack_probe_reason(report); !reason.empty()) {
        append_field(json, "reason", reason);
        if (!report.message.empty())
            append_field(json, "message", report.message);
    }
    json.push_back('}');
    std::string line{pack_probe_prefix};
    line += json;
    line.push_back('\n');
    return line;
}

} // namespace ayther::runtime
