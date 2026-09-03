#pragma once
// ---------------------------------------------------------------------------
// capture.h — synchronized comparison capture (#300).
//
// Three images from the SAME logical frame—the original, the AYTHER output,
// and the split view—plus a sidecar containing session metadata.
//
// Synchronization does not rely on coordinating two captures. All outputs are
// derived from one `FrameView`, rendered twice by `export_frame` without
// advancing emulation, then composed on the CPU. Keeping split composition on
// the CPU makes this invariant explicit and independently testable.
//
// Composition and JSON serialization are pure; only capture_write touches disk.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ayther {

/// Checked byte count for a tightly packed BGRA/RGBA image. Also enforces the
/// `int` width, height, and stride domain required by stb_image_write.
[[nodiscard]] std::optional<std::size_t>
capture_pixel_bytes(std::uint32_t width, std::uint32_t height) noexcept;

/// Metadata stored alongside a synchronized capture.
///
/// No field identifies a ROM file or its storage location. `game_id` is a
/// content checksum, while pack and profile values are pack-defined identifiers.
struct CaptureMeta {
    std::string game_id;     ///< "crc32:7b905383"
    std::string pack_name;
    std::string pack_build;  ///< Pack build identifier (#365).
    std::string profile;     ///< Active profile; empty means custom/no profile.
    std::string timestamp;   ///< ISO 8601 timestamp supplied by the caller.
    uint32_t    width  = 0;
    uint32_t    height = 0;
};

/// Compose a split view from two equally sized BGRA images.
///
/// Each half preserves its source pixels at their original positions; no
/// scaling occurs. This matches the live split-view contract (#298), so an
/// exported comparison represents exactly what the user saw.
///
/// @param dst Receives exactly `w * h * 4` bytes on success.
/// @param original Original BGRA image; must remain valid for the call.
/// @param ayther AYTHER BGRA image; must remain valid for the call.
/// @param w Image width in pixels.
/// @param h Image height in pixels.
/// @param split Divider position in the closed interval [0, 1].
/// @param vertical If true, split along the height; otherwise along the width.
/// @pre `w * h * 4` must be representable as `std::size_t`.
/// @return `false` if an input is null or either dimension is zero.
bool compose_split(std::vector<uint8_t>& dst,
                   const uint8_t* original, const uint8_t* ayther,
                   uint32_t w, uint32_t h, float split, bool vertical);

/// Serialize capture metadata for Hub (#317) and Lab (#310).
/// @return A complete JSON object encoded as UTF-8 text.
std::string capture_metadata_json(const CaptureMeta& m);

enum class CaptureWriteError {
    none,
    invalid_input,
    size_overflow,
    directory_error,
    target_exists,
    allocation_failed,
    image_write_failed,
    metadata_write_failed,
    rename_failed,
};

struct CaptureWriteResult final {
    std::string prefix;
    CaptureWriteError error{CaptureWriteError::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == CaptureWriteError::none;
    }
};

struct CaptureWriteOperations final {
    bool (*write_png)(const std::filesystem::path&, const std::uint8_t*,
                      std::uint32_t, std::uint32_t){nullptr};
    bool (*write_text)(const std::filesystem::path&, std::string_view){nullptr};
    bool (*rename_file)(const std::filesystem::path&,
                        const std::filesystem::path&){nullptr};
    void (*remove_file)(const std::filesystem::path&) noexcept{nullptr};
};

[[nodiscard]] const CaptureWriteOperations&
default_capture_write_operations() noexcept;

/// Transactional capture implementation with injectable file operations.
/// All artifacts are written to temporary paths and published only after the
/// complete set has been closed successfully. Any failure removes temporaries
/// and already-published members of the new set.
[[nodiscard]] CaptureWriteResult capture_write_transactional(
    const std::filesystem::path& dir, const std::string& base,
    const std::uint8_t* original, const std::uint8_t* ayther,
    std::uint32_t w, std::uint32_t h, float split, bool vertical,
    const CaptureMeta& meta,
    const CaptureWriteOperations& operations =
        default_capture_write_operations());

/// Write the original, AYTHER, split-view, and metadata capture artifacts.
///
/// Files are named `<base>-original.png`, `<base>-ayther.png`,
/// `<base>-split.png`, and `<base>.json` under `dir`. A shared prefix lets Lab
/// group the artifacts without a separate index that could become inconsistent.
///
/// @return The path prefix used for all files, or an empty string on failure.
///         Prefer capture_write_transactional() when the error category is
///         required for diagnostics.
std::string capture_write(const std::filesystem::path& dir,
                          const std::string& base,
                          const uint8_t* original, const uint8_t* ayther,
                          uint32_t w, uint32_t h, float split, bool vertical,
                          const CaptureMeta& meta);

}  // namespace ayther
