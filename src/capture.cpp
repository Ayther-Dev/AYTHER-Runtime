// ---------------------------------------------------------------------------
// capture.cpp — see capture.h for the public contract.
// ---------------------------------------------------------------------------
#include "capture.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

// Runtime owns its PNG writer. Keep stb's definitions local to this translation
// unit so they cannot collide with, or be satisfied by, implementation details
// from a statically linked Engine package.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace ayther {
namespace {

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x", c);
                    o += b;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

/// Convert readback BGRA pixels to the RGBA layout expected by the PNG writer.
std::vector<uint8_t> bgra_to_rgba(const uint8_t* src, uint32_t w, uint32_t h) {
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < out.size(); i += 4) {
        out[i + 0] = src[i + 2];
        out[i + 1] = src[i + 1];
        out[i + 2] = src[i + 0];
        out[i + 3] = 255;   // The rendered frame has no meaningful alpha.
    }
    return out;
}

}  // namespace

bool compose_split(std::vector<uint8_t>& dst,
                   const uint8_t* original, const uint8_t* ayther,
                   uint32_t w, uint32_t h, float split, bool vertical) {
    if (!original || !ayther || w == 0 || h == 0) return false;
    if (split < 0.0f) split = 0.0f;
    if (split > 1.0f) split = 1.0f;
    dst.resize(static_cast<size_t>(w) * h * 4);

    if (!vertical) {
        const uint32_t cut = static_cast<uint32_t>(w * split);
        for (uint32_t y = 0; y < h; ++y) {
            const size_t row = static_cast<size_t>(y) * w * 4;
            if (cut)     std::memcpy(&dst[row], &original[row], static_cast<size_t>(cut) * 4);
            if (cut < w) std::memcpy(&dst[row + static_cast<size_t>(cut) * 4],
                                     &ayther[row + static_cast<size_t>(cut) * 4],
                                     static_cast<size_t>(w - cut) * 4);
        }
    } else {
        const uint32_t cut = static_cast<uint32_t>(h * split);
        const size_t   stride = static_cast<size_t>(w) * 4;
        if (cut)     std::memcpy(&dst[0], original, cut * stride);
        if (cut < h) std::memcpy(&dst[cut * stride], &ayther[cut * stride],
                                 (h - cut) * stride);
    }
    return true;
}

std::string capture_metadata_json(const CaptureMeta& m) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"capture\": \"ayther.compare\",\n";
    o << "  \"capture_version\": 1,\n";
    o << "  \"game_id\": \"" << json_escape(m.game_id) << "\",\n";
    o << "  \"pack\": " << (m.pack_name.empty()
                                ? "null"
                                : "\"" + json_escape(m.pack_name) + "\"") << ",\n";
    o << "  \"pack_build\": " << (m.pack_build.empty()
                                      ? "null"
                                      : "\"" + json_escape(m.pack_build) + "\"") << ",\n";
    // An empty profile means the state matches no declared profile (#293).
    // Encode it as null so consumers can represent "custom" without inventing
    // an identifier.
    o << "  \"profile\": " << (m.profile.empty()
                                   ? "null"
                                   : "\"" + json_escape(m.profile) + "\"") << ",\n";
    o << "  \"timestamp\": \"" << json_escape(m.timestamp) << "\",\n";
    o << "  \"width\": " << m.width << ", \"height\": " << m.height << ",\n";
    o << "  \"images\": [\"original\", \"ayther\", \"split\"]\n";
    o << "}\n";
    return o.str();
}

std::string capture_write(const std::filesystem::path& dir,
                          const std::string& base,
                          const uint8_t* original, const uint8_t* ayther,
                          uint32_t w, uint32_t h, float split, bool vertical,
                          const CaptureMeta& meta) {
    if (!original || !ayther || w == 0 || h == 0) return {};
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const auto rgba_a = bgra_to_rgba(original, w, h);
    const auto rgba_b = bgra_to_rgba(ayther,   w, h);
    std::vector<uint8_t> split_bgra;
    if (!compose_split(split_bgra, original, ayther, w, h, split, vertical)) return {};
    const auto rgba_s = bgra_to_rgba(split_bgra.data(), w, h);

    auto png = [&](const char* suffix, const std::vector<uint8_t>& px) {
        const auto p = dir / (base + suffix + ".png");
        return stbi_write_png(p.string().c_str(), static_cast<int>(w),
                              static_cast<int>(h), 4, px.data(),
                              static_cast<int>(w) * 4) != 0;
    };
    // Remove PNG outputs if any image write fails. A partial image set is easy
    // to mistake for a complete comparison in a file browser.
    if (!png("-original", rgba_a) || !png("-ayther", rgba_b) || !png("-split", rgba_s)) {
        for (const char* s : { "-original", "-ayther", "-split" })
            std::filesystem::remove(dir / (base + s + ".png"), ec);
        return {};
    }

    const auto jp = dir / (base + ".json");
    std::ofstream jf(jp, std::ios::binary | std::ios::trunc);
    if (!jf) return {};
    const std::string js = capture_metadata_json(meta);
    jf.write(js.data(), static_cast<std::streamsize>(js.size()));
    return (dir / base).string();
}

}  // namespace ayther
