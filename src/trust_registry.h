#pragma once

#include <filesystem>
#include <string>

namespace ayther::runtime {

struct TrustRegistryConfig {
    std::string path;
    std::string diagnostic;
    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostic.empty();
    }
};

// Resolve once against the launch working directory. Engine owns trust
// decisions and re-reads this same file whenever it reopens the pack.
[[nodiscard]] TrustRegistryConfig resolve_trust_registry(
    const std::filesystem::path& path);

}  // namespace ayther::runtime
