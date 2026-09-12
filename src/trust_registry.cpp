#include "trust_registry.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <string_view>

namespace ayther::runtime {
namespace {

bool identifier(const std::string_view value, const bool game = false) {
    return !value.empty() && std::ranges::all_of(value, [game](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' ||
               (game && c == ':');
    });
}

std::string validate_shape(const toml::table& registry) {
    for (const auto& [key, value] : registry) {
        if (key != "version" && key != "keys")
            return "unknown registry field: " + std::string{key.str()};
    }
    if (!registry["version"].is_integer() || registry["version"].value<int>() != 1)
        return "version must be the integer 1";
    if (!registry.contains("keys")) return {};
    const auto* keys = registry["keys"].as_array();
    if (keys == nullptr) return "keys must be an array of tables ([[keys]])";
    std::set<std::string> ids;
    for (const auto& node : *keys) {
        const auto* key = node.as_table();
        if (key == nullptr) return "each keys entry must be a table";
        for (const auto& [field, value] : *key) {
            if (field != "id" && field != "algorithm" && field != "public_key" &&
                field != "not_before_unix" && field != "not_after_unix" &&
                field != "revoked" && field != "games")
                return "unknown key field: " + std::string{field.str()};
        }
        const auto id = (*key)["id"].value<std::string>();
        if (!id || id->size() > 64 || !identifier(*id))
            return "key id must contain 1-64 ASCII letters, digits, '.', '_' or '-'";
        if (!ids.insert(*id).second) return "duplicate key id: " + *id;
        if ((*key)["algorithm"].value<std::string>() != "ed25519")
            return "key '" + *id + "': algorithm must be ed25519";
        const auto public_key = (*key)["public_key"].value<std::string>();
        if (!public_key || public_key->size() != 64 ||
            !std::ranges::all_of(*public_key, [](const char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            })) return "key '" + *id + "': public_key must be 64 lowercase hex characters";
        const auto before = (*key)["not_before_unix"].value<std::int64_t>();
        const auto after = (*key)["not_after_unix"].value<std::int64_t>();
        if (!(*key)["not_before_unix"].is_integer() || !(*key)["not_after_unix"].is_integer() ||
            !before || !after || *before < 0 || *after < *before)
            return "key '" + *id + "': invalid Unix validity interval";
        if (key->contains("revoked") && !(*key)["revoked"].is_boolean())
            return "key '" + *id + "': revoked must be boolean";
        const auto* games = (*key)["games"].as_array();
        if (games == nullptr || games->empty())
            return "key '" + *id + "': games must be a nonempty string array";
        for (const auto& game : *games) {
            const auto scope = game.value<std::string>();
            if (!scope || (*scope != "*" && !identifier(*scope, true)))
                return "key '" + *id + "': invalid game scope";
        }
    }
    return {};
}

}  // namespace

TrustRegistryConfig resolve_trust_registry(const std::filesystem::path& path) {
    if (path.empty()) return {};
    TrustRegistryConfig result;
    const auto fail = [&](const std::string_view reason) {
        result.diagnostic = "--trust-registry '" + path.string() + "': " +
                            std::string{reason};
        return result;
    };
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
    if (error) return fail("cannot resolve path: " + error.message());
    result.path = absolute.string();
    if (!std::filesystem::is_regular_file(absolute, error))
        return fail("file is missing, inaccessible, or not a regular file");
    std::ifstream input{absolute, std::ios::binary};
    if (!input) return fail("file is not readable");
    try {
        const auto registry = toml::parse(input, result.path);
        if (input.bad()) return fail("I/O error while reading registry");
        const auto diagnostic = validate_shape(registry);
        if (!diagnostic.empty()) return fail(diagnostic);
    } catch (const toml::parse_error& parse_error) {
        return fail(std::string{"invalid TOML: "} + std::string{parse_error.description()});
    }
    return result;
}

}  // namespace ayther::runtime
