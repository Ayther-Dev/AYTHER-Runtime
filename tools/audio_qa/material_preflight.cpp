#include "material_preflight.h"

#include "content_hash.h"
#include "recording_header.h"
#include "replay_duration_limit.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ayther::audio_qa {
namespace {

constexpr std::size_t read_block_bytes = 256U * 1024U;
constexpr std::uint64_t unlimited_bytes = ~std::uint64_t{};

constexpr std::array<std::uint32_t, 256> crc32_table = [] {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < table.size(); ++index) {
        std::uint32_t value = index;
        for (int bit = 0; bit < 8; ++bit)
            value = (value & 1U) != 0U ? (value >> 1U) ^ 0xedb88320U : value >> 1U;
        table[index] = value;
    }
    return table;
}();

// CRC-32 (IEEE 802.3), the checksum Play CE and the packs use to name a ROM.
class Crc32 final {
  public:
    void update(std::span<const std::byte> bytes) noexcept {
        for (const auto byte : bytes)
            state_ = crc32_table[(state_ ^ std::to_integer<std::uint32_t>(byte)) & 0xffU] ^
                     (state_ >> 8U);
    }
    [[nodiscard]] std::uint32_t finish() const noexcept { return state_ ^ 0xffffffffU; }

  private:
    std::uint32_t state_{0xffffffffU};
};

struct Identified {
    ContentIdentity content;
    std::uint32_t crc32{};
    FileIdentity file;
};

using Identification = std::variant<Identified, MaterialPinError>;

// Hashes every block read through the pinning handle and, when asked, keeps them.
class ContentSink final {
  public:
    explicit ContentSink(std::vector<std::byte> *bytes) noexcept : bytes_(bytes) {}

    void reserve(std::uint64_t size) {
        if (bytes_ != nullptr)
            bytes_->reserve(static_cast<std::size_t>(size));
    }

    void update(std::span<const std::byte> block) {
        hasher_.update(block);
        crc32_.update(block);
        total_ += block.size();
        if (bytes_ != nullptr)
            bytes_->insert(bytes_->end(), block.begin(), block.end());
    }

    [[nodiscard]] std::uint64_t total() const noexcept { return total_; }

    void finish(Identified &identified) const noexcept {
        identified.content = hasher_.finish();
        identified.crc32 = crc32_.finish();
    }

  private:
    std::vector<std::byte> *bytes_;
    ContentHasher hasher_;
    Crc32 crc32_;
    std::uint64_t total_{};
};

#ifdef _WIN32

class OwnedHandle final {
  public:
    OwnedHandle() noexcept = default;
    explicit OwnedHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~OwnedHandle() { reset(); }
    OwnedHandle(const OwnedHandle &) = delete;
    OwnedHandle &operator=(const OwnedHandle &) = delete;
    OwnedHandle(OwnedHandle &&other) noexcept
        : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
    OwnedHandle &operator=(OwnedHandle &&other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept {
        return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

  private:
    void reset() noexcept {
        if (valid())
            CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }

    HANDLE handle_{INVALID_HANDLE_VALUE};
};

MaterialPinError open_error(DWORD error) noexcept {
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
        return MaterialPinError::not_found;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return MaterialPinError::in_use;
    default:
        return MaterialPinError::unreadable;
    }
}

// Read access shared only for reading: while the handle is open nobody can write,
// rename or delete the material, and the open fails if a writer already has it.
OwnedHandle open_shared_for_reading(const std::filesystem::path &path, bool follow_reparse_point) {
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN;
    if (!follow_reparse_point)
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    return OwnedHandle{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, flags, nullptr)};
}

using OwnedFile = OwnedHandle;

// `keep`, when given, receives the handle the identity was computed through.
Identification identify(const std::filesystem::path &path, std::vector<std::byte> *bytes,
                        std::uint64_t max_bytes, OwnedFile *keep = nullptr) {
    auto handle = open_shared_for_reading(path, false);
    if (!handle.valid())
        return open_error(GetLastError());
    if (GetFileType(handle.get()) != FILE_TYPE_DISK)
        return MaterialPinError::not_regular;
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (GetFileInformationByHandleEx(handle.get(), FileAttributeTagInfo, &tag, sizeof(tag)) == 0)
        return MaterialPinError::unreadable;
    if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        // Symbolic links and junctions name another file. Other reparse points, such as
        // cloud placeholders, hold the file itself and are read through it.
        if (IsReparseTagNameSurrogate(tag.ReparseTag))
            return MaterialPinError::is_link;
        handle = open_shared_for_reading(path, true);
        if (!handle.valid())
            return open_error(GetLastError());
    }
    if ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U)
        return MaterialPinError::is_directory;

    BY_HANDLE_FILE_INFORMATION information{};
    if (GetFileInformationByHandle(handle.get(), &information) == 0)
        return MaterialPinError::unreadable;
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U)
        return MaterialPinError::is_directory;

    Identified identified;
    FILE_ID_INFO file_id{};
    if (GetFileInformationByHandleEx(handle.get(), FileIdInfo, &file_id, sizeof(file_id)) != 0) {
        identified.file.volume_serial = file_id.VolumeSerialNumber;
        static_assert(sizeof(file_id.FileId.Identifier) == sizeof(identified.file.file_id));
        std::memcpy(identified.file.file_id.data(), file_id.FileId.Identifier,
                    sizeof(file_id.FileId.Identifier));
    } else {
        identified.file.volume_serial = information.dwVolumeSerialNumber;
        const auto index = (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) |
                           information.nFileIndexLow;
        std::memcpy(identified.file.file_id.data(), &index, sizeof(index));
    }
    identified.file.last_write_time = static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(information.ftLastWriteTime.dwHighDateTime) << 32U) |
        information.ftLastWriteTime.dwLowDateTime);
    const auto size =
        (static_cast<std::uint64_t>(information.nFileSizeHigh) << 32U) | information.nFileSizeLow;
    if (size > max_bytes)
        return MaterialPinError::too_large;

    ContentSink sink{bytes};
    sink.reserve(bytes != nullptr ? size : 0U);
    std::vector<std::byte> buffer(read_block_bytes);
    for (;;) {
        DWORD read{};
        if (ReadFile(handle.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                     nullptr) == 0)
            return MaterialPinError::unreadable;
        if (read == 0)
            break;
        sink.update(std::span<const std::byte>{buffer}.first(read));
    }
    if (sink.total() != size)
        return MaterialPinError::unreadable;
    sink.finish(identified);
    if (keep != nullptr)
        *keep = std::move(handle);
    return identified;
}

#else

class OwnedDescriptor final {
  public:
    OwnedDescriptor() noexcept = default;
    explicit OwnedDescriptor(int descriptor) noexcept : descriptor_(descriptor) {}
    ~OwnedDescriptor() { reset(); }
    OwnedDescriptor(const OwnedDescriptor &) = delete;
    OwnedDescriptor &operator=(const OwnedDescriptor &) = delete;
    OwnedDescriptor(OwnedDescriptor &&other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)) {}
    OwnedDescriptor &operator=(OwnedDescriptor &&other) noexcept {
        if (this != &other) {
            reset();
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }
    [[nodiscard]] int get() const noexcept { return descriptor_; }

  private:
    void reset() noexcept {
        if (descriptor_ >= 0)
            ::close(descriptor_);
        descriptor_ = -1;
    }

    int descriptor_{-1};
};

using OwnedFile = OwnedDescriptor;

Identification identify(const std::filesystem::path &path, std::vector<std::byte> *bytes,
                        std::uint64_t max_bytes, OwnedFile *keep = nullptr) {
    OwnedDescriptor descriptor{::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)};
    if (descriptor.get() < 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return MaterialPinError::not_found;
        if (errno == ELOOP)
            return MaterialPinError::is_link;
        return MaterialPinError::unreadable;
    }
    struct stat information = {};
    if (::fstat(descriptor.get(), &information) != 0)
        return MaterialPinError::unreadable;
    if (S_ISDIR(information.st_mode))
        return MaterialPinError::is_directory;
    if (!S_ISREG(information.st_mode))
        return MaterialPinError::not_regular;

    Identified identified;
    identified.file.volume_serial = static_cast<std::uint64_t>(information.st_dev);
    const auto inode = static_cast<std::uint64_t>(information.st_ino);
    std::memcpy(identified.file.file_id.data(), &inode, sizeof(inode));
    identified.file.last_write_time = static_cast<std::int64_t>(information.st_mtime);
    const auto size = static_cast<std::uint64_t>(information.st_size);
    if (size > max_bytes)
        return MaterialPinError::too_large;

    ContentSink sink{bytes};
    sink.reserve(bytes != nullptr ? size : 0U);
    std::vector<std::byte> buffer(read_block_bytes);
    for (;;) {
        const auto read = ::read(descriptor.get(), buffer.data(), buffer.size());
        if (read < 0)
            return MaterialPinError::unreadable;
        if (read == 0)
            break;
        sink.update(std::span<const std::byte>{buffer}.first(static_cast<std::size_t>(read)));
    }
    if (sink.total() != size)
        return MaterialPinError::unreadable;
    sink.finish(identified);
    if (keep != nullptr)
        *keep = std::move(descriptor);
    return identified;
}

#endif

std::string text_at(std::span<const std::byte> bytes, const RecordingByteRange &range) {
    const auto text =
        bytes.subspan(static_cast<std::size_t>(range.offset), static_cast<std::size_t>(range.size));
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

std::string_view take_header_code(RecordingHeaderError error) noexcept {
    switch (error) {
    case RecordingHeaderError::none:
        break;
    case RecordingHeaderError::empty_take:
        return "take_empty";
    case RecordingHeaderError::truncated_header:
        return "take_truncated_header";
    case RecordingHeaderError::invalid_magic:
        return "take_invalid_magic";
    case RecordingHeaderError::unsupported_version:
        return "take_unsupported_version";
    }
    return "take_invalid_header";
}

std::string_view take_layout_code(RecordingLayoutError error) noexcept {
    switch (error) {
    case RecordingLayoutError::none:
    case RecordingLayoutError::invalid_header:
        break;
    case RecordingLayoutError::take_too_large:
        return "take_too_large";
    case RecordingLayoutError::truncated_content:
        return "take_truncated";
    case RecordingLayoutError::invalid_frame_count:
        return "take_invalid_frame_count";
    case RecordingLayoutError::empty_initial_state:
        return "take_empty_initial_state";
    case RecordingLayoutError::initial_state_too_large:
        return "take_initial_state_too_large";
    case RecordingLayoutError::empty_compressed_state:
        return "take_empty_compressed_state";
    }
    return "take_invalid_layout";
}

} // namespace

MaterialPinResult pin_material(MaterialPinRole role, std::string field,
                               const std::filesystem::path &path, std::size_t position) noexcept {
    try {
        auto identification = identify(path, nullptr, unlimited_bytes);
        if (const auto *error = std::get_if<MaterialPinError>(&identification))
            return MaterialIssue{std::move(field), *error};
        const auto &identified = std::get<Identified>(identification);
        return MaterialPin{role,           std::move(field),   position,
                           path,           identified.content, identified.crc32,
                           identified.file};
    } catch (...) {
        return MaterialIssue{std::move(field), MaterialPinError::unreadable};
    }
}

PinnedContentResult pin_material_content(MaterialPinRole role, std::string field,
                                         const std::filesystem::path &path, std::size_t position,
                                         std::uint64_t max_bytes) noexcept {
    try {
        PinnedContent pinned;
        auto identification = identify(path, &pinned.bytes, max_bytes);
        if (const auto *error = std::get_if<MaterialPinError>(&identification))
            return MaterialIssue{std::move(field), *error};
        const auto &identified = std::get<Identified>(identification);
        pinned.pin = MaterialPin{role,           std::move(field),   position,
                                 path,           identified.content, identified.crc32,
                                 identified.file};
        return pinned;
    } catch (...) {
        return MaterialIssue{std::move(field), MaterialPinError::unreadable};
    }
}

struct HeldMaterials::Files {
    std::vector<OwnedFile> files;
};

HeldMaterials::HeldMaterials() = default;
HeldMaterials::~HeldMaterials() = default;
HeldMaterials::HeldMaterials(HeldMaterials &&other) noexcept = default;
HeldMaterials &HeldMaterials::operator=(HeldMaterials &&other) noexcept = default;

std::size_t HeldMaterials::size() const noexcept {
    return files_ != nullptr ? files_->files.size() : 0U;
}

HoldResult hold_materials(std::span<const MaterialPin> pins) noexcept {
    try {
        HeldMaterials held;
        held.files_ = std::make_unique<HeldMaterials::Files>();
        held.files_->files.reserve(pins.size());
        for (const auto &pin : pins) {
            OwnedFile file;
            const auto identification = identify(pin.path, nullptr, unlimited_bytes, &file);
            if (const auto *error = std::get_if<MaterialPinError>(&identification)) {
                switch (*error) {
                case MaterialPinError::not_found:
                case MaterialPinError::unreadable:
                    return FieldIssue{pin.field, "material_unavailable"};
                case MaterialPinError::in_use:
                    return FieldIssue{pin.field, "material_in_use"};
                case MaterialPinError::is_directory:
                case MaterialPinError::is_link:
                case MaterialPinError::not_regular:
                case MaterialPinError::too_large:
                    return FieldIssue{pin.field, "material_changed"};
                }
            }
            if (std::get<Identified>(identification).content != pin.content)
                return FieldIssue{pin.field, "material_changed"};
            held.files_->files.push_back(std::move(file));
        }
        return held;
    } catch (...) {
        return FieldIssue{"", "material_unavailable"};
    }
}

std::string_view material_pin_error_code(MaterialPinError error) noexcept {
    switch (error) {
    case MaterialPinError::not_found:
        return "material_not_found";
    case MaterialPinError::is_directory:
        return "material_is_directory";
    case MaterialPinError::is_link:
        return "material_is_link";
    case MaterialPinError::not_regular:
        return "material_not_regular";
    case MaterialPinError::in_use:
        return "material_in_use";
    case MaterialPinError::too_large:
        return "material_too_large";
    case MaterialPinError::unreadable:
        return "material_unreadable";
    }
    return "material_unreadable";
}

std::string_view material_pin_role_code(MaterialPinRole role) noexcept {
    switch (role) {
    case MaterialPinRole::rom:
        return "rom";
    case MaterialPinRole::core:
        return "core";
    case MaterialPinRole::pack:
        return "pack";
    case MaterialPinRole::take:
        return "take";
    case MaterialPinRole::runtime:
        return "runtime";
    case MaterialPinRole::trust_registry:
        return "trust_registry";
    }
    return "unknown";
}

FieldIssue field_issue(const MaterialIssue &issue) {
    return {issue.field, std::string{material_pin_error_code(issue.error)}};
}

TakeInspection inspect_take(std::span<const std::byte> bytes) {
    const auto decoded = decode_recording_layout(bytes);
    if (decoded.error == RecordingLayoutError::invalid_header)
        return std::string{take_header_code(decoded.header_error)};
    if (decoded.error != RecordingLayoutError::none)
        return std::string{take_layout_code(decoded.error)};
    return TakeFacts{decoded.layout.header.version, decoded.layout.frame_count,
                     text_at(bytes, decoded.layout.game_id), text_at(bytes, decoded.layout.name)};
}

std::optional<FieldIssue> take_duration_issue(std::uint32_t frame_count,
                                              std::optional<double> timing_fps) {
    const auto duration = validate_replay_duration(frame_count, timing_fps.value_or(0.0));
    switch (duration.error) {
    case ReplayDurationError::none:
        return std::nullopt;
    case ReplayDurationError::frame_count_out_of_range:
        return FieldIssue{"--take", "take_invalid_frame_count"};
    case ReplayDurationError::invalid_declared_fps:
        return FieldIssue{"--core", "core_timing_unknown"};
    case ReplayDurationError::duration_too_long:
        return FieldIssue{"--take", "take_too_long"};
    }
    return FieldIssue{"--take", "take_too_long"};
}

std::string crc32_game_id(std::uint32_t crc32) {
    std::array<char, 16> text{};
    std::snprintf(text.data(), text.size(), "crc32:%08x", static_cast<unsigned>(crc32));
    return text.data();
}

namespace {

constexpr std::string_view crc32_prefix = "crc32:";

// `crc32:<hex>` names the ROM whatever the case of its digits.
bool names_rom(std::string_view game_id, std::uint32_t rom_crc32) {
    std::string lowered{game_id};
    for (auto &character : lowered)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    return lowered == crc32_game_id(rom_crc32);
}

} // namespace

std::optional<std::string> take_game_issue(std::string_view take_game_id, std::uint32_t rom_crc32,
                                           std::optional<std::string_view> pack_game_id) {
    if (take_game_id.empty())
        return std::nullopt;
    if (take_game_id.starts_with(crc32_prefix)) {
        if (names_rom(take_game_id, rom_crc32))
            return std::nullopt;
        return "take_game_mismatch";
    }
    if (pack_game_id && !pack_game_id->empty() && *pack_game_id != take_game_id)
        return "take_game_mismatch";
    return std::nullopt;
}

std::optional<std::string> pack_game_issue(std::string_view pack_game_id, std::uint32_t rom_crc32) {
    if (!pack_game_id.starts_with(crc32_prefix) || names_rom(pack_game_id, rom_crc32))
        return std::nullopt;
    return "pack_game_mismatch";
}

} // namespace ayther::audio_qa
