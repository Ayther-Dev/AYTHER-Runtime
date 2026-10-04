#include "pack_probe.h"

#include <ayther/ayther_core_ffi.h>
#include <ayther/ayther_result.h>
#include <ayther/engine/pack.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ayther::runtime {
namespace {

struct ArchiveCloser {
    void operator()(AyArchive *archive) const noexcept { ayther_pack_close(archive); }
};

using Archive = std::unique_ptr<AyArchive, ArchiveCloser>;

struct AbsentEntry {};
struct UnreadableEntry {};
using Entry = std::variant<std::vector<std::uint8_t>, AbsentEntry, UnreadableEntry>;

// Every read goes through the Engine, which verifies it against the signed pack index.
Entry read_entry(const AyArchive *archive, const std::string &path) {
    const auto size = ayther_pack_file_size(archive, path.c_str());
    if (size < 0)
        return AbsentEntry{};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (ayther_pack_read(archive, path.c_str(), bytes.data(), bytes.size()) != size)
        return UnreadableEntry{};
    return bytes;
}

// The catalog text, empty when the pack has none; nullopt when it cannot be read.
std::optional<std::string> catalog_text(const AyArchive *archive, const std::string &path) {
    const auto entry = read_entry(archive, path);
    if (std::holds_alternative<AbsentEntry>(entry))
        return std::string{};
    const auto *bytes = std::get_if<std::vector<std::uint8_t>>(&entry);
    if (bytes == nullptr)
        return std::nullopt;
    return std::string{bytes->begin(), bytes->end()};
}

bool asset_readable(const AyArchive *archive, const std::string &asset) {
    const auto entry = read_entry(archive, asset);
    const auto *bytes = std::get_if<std::vector<std::uint8_t>>(&entry);
    return bytes != nullptr && asset_decodes(*bytes);
}

} // namespace

PackProbeReport probe_pack(const std::string &pack, const std::string &trust_registry) {
    PackProbeReport report;

    // The pack as a file: the Engine validation, without opening it for a session.
    const auto validation = ayther::engine::validate_pack(pack);
    if (!validation) {
        report.errors.emplace_back("validation.unavailable");
        report.message = validation.error.message;
        return report;
    }
    report.signed_pack =
        std::none_of(validation->findings.begin(), validation->findings.end(),
                     [](const auto &finding) { return finding.code == "signature.missing"; });
    for (const auto &finding : validation->findings) {
        if (!finding.is_error())
            continue;
        report.errors.push_back(finding.code);
        if (report.message.empty())
            report.message = finding.message;
    }

    // Opening, signature and trust: a registry vouches for the signature; without one a
    // signed pack stays unverified.
    const auto inspected = ayther::engine::inspect_pack(pack, trust_registry);
    report.opened = static_cast<bool>(inspected);
    if (inspected)
        report.game_id = inspected->game_id;
    else if (report.message.empty())
        report.message = inspected.error.message;
    if (!report.signed_pack)
        report.signature = "unsigned";
    else if (trust_registry.empty())
        report.signature = "unverified";
    else if (report.opened)
        report.signature = "valid";
    else if (inspected.error.code == ayther::ErrorCode::BadSignature)
        report.signature = "invalid";
    report.trust =
        trust_registry.empty() ? "unverified" : (report.opened ? "trusted" : "untrusted");
    if (!report.opened)
        return report;

    // Catalogs and the assets they name, in every resolution tier the pack declares.
    const Archive archive{trust_registry.empty()
                              ? ayther_pack_open(pack.c_str())
                              : ayther_pack_open_trusted(pack.c_str(), trust_registry.c_str())};
    if (!archive) {
        report.opened = false;
        return report;
    }
    const auto poses = catalog_text(archive.get(), "pose_substitutions.toml");
    const auto audio_events = catalog_text(archive.get(), "audio_events.toml");
    if (!poses || !audio_events)
        return report;
    report.catalog = read_pack_catalog(*poses, *audio_events);
    if (!report.catalog)
        return report;
    const auto tiers = ayther_pack_tiers(archive.get());
    std::vector<int> checked_tiers;
    for (int tier = 0; tier < 8; ++tier)
        if ((tiers & (1U << static_cast<unsigned>(tier))) != 0U)
            checked_tiers.push_back(tier);
    if (checked_tiers.empty())
        checked_tiers.push_back(-1);
    for (const int tier : checked_tiers) {
        if (tier >= 0)
            ayther_pack_set_tier(archive.get(), tier);
        for (const auto &asset : report.catalog->assets) {
            if (std::find(report.unreadable_assets.begin(), report.unreadable_assets.end(),
                          asset) != report.unreadable_assets.end())
                continue;
            if (!asset_readable(archive.get(), asset))
                report.unreadable_assets.push_back(asset);
        }
    }
    return report;
}

} // namespace ayther::runtime
