#pragma once

#include <filesystem>
#include <string>
#include <system_error>

namespace ayther::audio_qa {

// Spec 002, D-12 (campaign 2026-10-05): the evidence of a take lives under
// `<output>/runs/<run_id>/fragments/facts-<20 digits>.aqf`. With a long destination that path
// passes the 260 characters of MAX_PATH while the run directory does not, and a process that
// is not long-path aware cannot create the fragment: the whole take loses its facts. Every
// file system call of the evidence store goes through the extended-length form (`\\?\`),
// which has no such limit. The path a caller sees, prints or stores does not change.
//
// Absolute drive and UNC paths are made absolute, normalized and prefixed; a path that is
// already extended or a device path is kept. Elsewhere it is the same path.
[[nodiscard]] inline std::filesystem::path long_path(const std::filesystem::path &path) noexcept {
#ifdef _WIN32
    try {
        const std::wstring &text = path.native();
        if (text.empty() || text.starts_with(LR"(\\?\)") || text.starts_with(LR"(\\.\)"))
            return path;
        std::error_code error;
        const auto absolute = std::filesystem::absolute(path, error);
        if (error)
            return path;
        auto normal = absolute.lexically_normal();
        normal.make_preferred();
        const std::wstring &full = normal.native();
        if (full.starts_with(LR"(\\)"))
            return std::filesystem::path{LR"(\\?\UNC\)" + full.substr(2U)};
        if (full.size() >= 3U && full[1] == L':' && full[2] == L'\\')
            return std::filesystem::path{LR"(\\?\)" + full};
        return path;
    } catch (...) {
        return path;
    }
#else
    return path;
#endif
}

} // namespace ayther::audio_qa
