#pragma once

#include "field_issue.h"
#include "reference_model.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

// Spec 002, plan §4.1 and §5.2 (RF-2.2, RF-2.11): the identity of each selected material
// is fixed before the request is admitted, and the take uses exactly that content.
enum class MaterialPinRole { rom, core, pack, take, runtime, trust_registry };

// Which file a path named when it was pinned: replacing the file changes it even when
// the new content is the same.
struct FileIdentity {
    std::uint64_t volume_serial{};
    std::array<std::uint8_t, 16> file_id{};
    std::int64_t last_write_time{};
    bool operator==(const FileIdentity &) const = default;
};

struct MaterialPin {
    MaterialPinRole role{MaterialPinRole::rom};
    std::string field;
    std::size_t position{};
    std::filesystem::path path;
    ContentIdentity content;
    std::uint32_t crc32{};
    FileIdentity file;
    bool operator==(const MaterialPin &) const = default;
};

enum class MaterialPinError {
    not_found,
    is_directory,
    is_link,
    not_regular,
    in_use,
    too_large,
    unreadable,
};

// A rejected material names the option that selected it (RF-1.7).
struct MaterialIssue {
    std::string field;
    MaterialPinError error{MaterialPinError::not_found};
    bool operator==(const MaterialIssue &) const = default;
};

using MaterialPinResult = std::variant<MaterialPin, MaterialIssue>;

// Opens `path` for reading only, without following a link, and computes its SHA-256,
// CRC-32, size and file identity through the same handle. Directories, links, devices
// and missing files are rejected. Nothing is written.
[[nodiscard]] MaterialPinResult pin_material(MaterialPinRole role, std::string field,
                                             const std::filesystem::path &path,
                                             std::size_t position = 0) noexcept;

// The same pin plus the bytes it was computed from, for materials that are validated by
// content. A material larger than `max_bytes` is rejected before it is read.
struct PinnedContent {
    MaterialPin pin;
    std::vector<std::byte> bytes;
};

using PinnedContentResult = std::variant<PinnedContent, MaterialIssue>;

[[nodiscard]] PinnedContentResult pin_material_content(MaterialPinRole role, std::string field,
                                                       const std::filesystem::path &path,
                                                       std::size_t position,
                                                       std::uint64_t max_bytes) noexcept;

// RF-2.11 (plan §5.2): while a take runs its materials stay open for reading only, so
// nobody can write, rename or delete them, and their content is checked against the pin
// before the Runtime receives them. A difference stops the request with
// `material_changed` before the new content is used; a material that is gone gives
// `material_unavailable`. On POSIX the content is checked but writers cannot be blocked.
class HeldMaterials final {
  public:
    HeldMaterials();
    ~HeldMaterials();
    HeldMaterials(HeldMaterials &&other) noexcept;
    HeldMaterials &operator=(HeldMaterials &&other) noexcept;
    HeldMaterials(const HeldMaterials &) = delete;
    HeldMaterials &operator=(const HeldMaterials &) = delete;

    [[nodiscard]] std::size_t size() const noexcept;

  private:
    struct Files;
    std::unique_ptr<Files> files_;

    friend std::variant<HeldMaterials, FieldIssue>
    hold_materials(std::span<const MaterialPin> pins) noexcept;
};

using HoldResult = std::variant<HeldMaterials, FieldIssue>;

[[nodiscard]] HoldResult hold_materials(std::span<const MaterialPin> pins) noexcept;

[[nodiscard]] std::string_view material_pin_error_code(MaterialPinError error) noexcept;
[[nodiscard]] std::string_view material_pin_role_code(MaterialPinRole role) noexcept;
[[nodiscard]] FieldIssue field_issue(const MaterialIssue &issue);

// RF-2.2, RNF-3: what the take declares, read only after its layout is valid: ARP1
// versions 2 to 8, 1 to 54 000 frames, 64 MiB and input ranges inside the file.
struct TakeFacts {
    std::uint32_t version{};
    std::uint32_t frame_count{};
    std::string game_id;
    std::string name;
    bool operator==(const TakeFacts &) const = default;
};

using TakeInspection = std::variant<TakeFacts, std::string>;

[[nodiscard]] TakeInspection inspect_take(std::span<const std::byte> bytes);

// RF-2.2 (D-2 of the 2026-10-04 campaign): the initial state of a take whose layout is valid
// decompresses whole, as the Runtime will decompress it before restoring it
// (`decompress_recording_state`). `take_initial_state_invalid` otherwise.
[[nodiscard]] std::optional<std::string> take_state_issue(std::span<const std::byte> bytes);

// RF-2.2, RNF-3 (contracts.md C5, «Sondeo del core con la ROM»): N / timing_fps must not
// exceed 900 s, with the timing the core reports for that ROM; at 59.92 fps 54 000 frames
// already do. A missing or invalid timing is a problem of the core.
[[nodiscard]] std::optional<FieldIssue> take_duration_issue(std::uint32_t frame_count,
                                                            std::optional<double> timing_fps);

// Game identities written as `crc32:<8 hex digits>` name the ROM by its CRC-32.
[[nodiscard]] std::string crc32_game_id(std::uint32_t crc32);

// RF-1.7, RF-2.2: the take and the pack must belong to the selected ROM. A take names
// the game of the pack it was recorded with; it is compared with the ROM when it names
// it by CRC-32 and with the selected pack otherwise. Without either there is nothing to
// compare with, and a take recorded with a pack can be replayed without it (RF-1.3).
[[nodiscard]] std::optional<std::string>
take_game_issue(std::string_view take_game_id, std::uint32_t rom_crc32,
                std::optional<std::string_view> pack_game_id);
[[nodiscard]] std::optional<std::string> pack_game_issue(std::string_view pack_game_id,
                                                         std::uint32_t rom_crc32);

} // namespace ayther::audio_qa
