#include "reference_store.h"

#include "content_hash.h"
#include "model_limits.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ayther::audio_qa {
namespace {

constexpr std::array origin_names{
    "unknown",           "user_declaration",    "configuration",
    "artifact_manifest", "content_measurement", "runtime_observation"};
constexpr std::array role_names{"rom",          "core",         "pack", "take", "external_asset",
                                "engine_build", "runtime_build"};
constexpr std::array stability_names{"unverified", "verified", "changed"};
constexpr std::array reference_role_names{"initial", "extended"};
constexpr char hex_digits[] = "0123456789abcdef";

enum class ExclusiveWriteResult { written, already_exists, io_error };

template <typename Enum, std::size_t Size>
const char *enum_name(const Enum value, const std::array<const char *, Size> &names) {
    const auto index = static_cast<std::size_t>(value);
    if (index >= names.size()) {
        throw ReferenceStoreError::invalid_reference;
    }
    return names[index];
}

template <typename Enum, std::size_t Size>
Enum parse_enum(const toml::table &table, const std::string_view key,
                const std::array<const char *, Size> &names) {
    const auto value = table[key].value<std::string>();
    if (!value) {
        throw ReferenceStoreError::invalid_document;
    }
    for (std::size_t index{}; index < names.size(); ++index) {
        if (*value == names[index]) {
            return static_cast<Enum>(index);
        }
    }
    throw ReferenceStoreError::invalid_document;
}

std::string hex(const std::array<std::uint8_t, 32> &bytes) {
    std::string result(bytes.size() * 2, '0');
    for (std::size_t index{}; index < bytes.size(); ++index) {
        result[index * 2] = hex_digits[bytes[index] >> 4U];
        result[index * 2 + 1] = hex_digits[bytes[index] & 0x0FU];
    }
    return result;
}

std::optional<std::array<std::uint8_t, 32>> parse_hex(const std::string_view value) noexcept {
    if (value.size() != 64) {
        return std::nullopt;
    }
    const auto nibble = [](const char character) -> std::optional<std::uint8_t> {
        if (character >= '0' && character <= '9') {
            return static_cast<std::uint8_t>(character - '0');
        }
        if (character >= 'a' && character <= 'f') {
            return static_cast<std::uint8_t>(character - 'a' + 10);
        }
        return std::nullopt;
    };
    std::array<std::uint8_t, 32> result{};
    for (std::size_t index{}; index < result.size(); ++index) {
        const auto high = nibble(value[index * 2]);
        const auto low = nibble(value[index * 2 + 1]);
        if (!high || !low) {
            return std::nullopt;
        }
        result[index] = static_cast<std::uint8_t>((*high << 4U) | *low);
    }
    return result;
}

std::optional<std::uint64_t> parse_unsigned(const std::string_view value) noexcept {
    if (value.empty() || value.front() == '+' || value.front() == '-') {
        return std::nullopt;
    }
    std::uint64_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return result;
}

toml::table provenance_table(const Provenance &provenance) {
    return toml::table{
        {"origin", enum_name(provenance.origin, origin_names)},
        {"evidence_id", provenance.evidence_id},
    };
}

Provenance parse_provenance(const toml::table &table) {
    Provenance result;
    result.origin = parse_enum<Origin>(table, "origin", origin_names);
    const auto evidence = table["evidence_id"].value<std::string>();
    if (!evidence) {
        throw ReferenceStoreError::invalid_document;
    }
    result.evidence_id = *evidence;
    return result;
}

toml::table recorded_text(const Recorded<std::string> &recorded) {
    auto result = provenance_table(recorded.provenance);
    result.insert("available", recorded.value.has_value());
    if (recorded.value) {
        result.insert("value", *recorded.value);
    }
    return result;
}

Recorded<std::string> parse_recorded_text(const toml::table &table) {
    const auto available = table["available"].value<bool>();
    if (!available) {
        throw ReferenceStoreError::invalid_document;
    }
    Recorded<std::string> result;
    result.provenance = parse_provenance(table);
    if (*available) {
        const auto value = table["value"].value<std::string>();
        if (!value) {
            throw ReferenceStoreError::invalid_document;
        }
        result.value = *value;
    } else if (table.contains("value")) {
        throw ReferenceStoreError::invalid_document;
    }
    return result;
}

toml::table recorded_identity(const Recorded<ContentIdentity> &recorded) {
    auto result = provenance_table(recorded.provenance);
    result.insert("available", recorded.value.has_value());
    if (recorded.value) {
        result.insert("sha256", hex(recorded.value->sha256));
        result.insert("byte_size", std::to_string(recorded.value->byte_size));
    }
    return result;
}

Recorded<ContentIdentity> parse_recorded_identity(const toml::table &table) {
    const auto available = table["available"].value<bool>();
    if (!available) {
        throw ReferenceStoreError::invalid_document;
    }
    Recorded<ContentIdentity> result;
    result.provenance = parse_provenance(table);
    if (*available) {
        const auto digest_text = table["sha256"].value<std::string>();
        const auto size_text = table["byte_size"].value<std::string>();
        if (!digest_text || !size_text) {
            throw ReferenceStoreError::invalid_document;
        }
        const auto digest = parse_hex(*digest_text);
        const auto size = parse_unsigned(*size_text);
        if (!digest || !size) {
            throw ReferenceStoreError::invalid_document;
        }
        result.value = ContentIdentity{*digest, *size};
    } else if (table.contains("sha256") || table.contains("byte_size")) {
        throw ReferenceStoreError::invalid_document;
    }
    return result;
}

const toml::table &required_table(const toml::table &parent, const std::string_view key) {
    const auto *value = parent[key].as_table();
    if (value == nullptr) {
        throw ReferenceStoreError::invalid_document;
    }
    return *value;
}

std::string required_text(const toml::table &table, const std::string_view key) {
    const auto value = table[key].value<std::string>();
    if (!value) {
        throw ReferenceStoreError::invalid_document;
    }
    return *value;
}

toml::table build_table(const BuildReference &build) {
    return toml::table{{"artifact", recorded_identity(build.artifact)},
                       {"release", recorded_text(build.release)},
                       {"commit", recorded_text(build.commit)},
                       {"variant", recorded_text(build.variant)},
                       {"abi", recorded_text(build.abi)}};
}

BuildReference parse_build(const toml::table &table) {
    BuildReference result;
    result.artifact = parse_recorded_identity(required_table(table, "artifact"));
    result.release = parse_recorded_text(required_table(table, "release"));
    result.commit = parse_recorded_text(required_table(table, "commit"));
    result.variant = parse_recorded_text(required_table(table, "variant"));
    result.abi = parse_recorded_text(required_table(table, "abi"));
    return result;
}

toml::table material_table(const Material &material) {
    auto result = toml::table{
        {"material_id", material.material_id},
        {"role", enum_name(material.role, role_names)},
        {"stability", enum_name(material.stability, stability_names)},
        {"source_locator", recorded_text(material.source_locator)},
        {"format_version", recorded_text(material.format_version)},
        {"identified", recorded_identity(material.identified)},
        {"consumed", recorded_identity(material.consumed)},
        {"stability_provenance", provenance_table(material.stability_provenance)},
    };
    return result;
}

Material parse_material(const toml::table &table) {
    Material result;
    result.material_id = required_text(table, "material_id");
    result.role = parse_enum<MaterialRole>(table, "role", role_names);
    result.stability = parse_enum<Stability>(table, "stability", stability_names);
    result.source_locator = parse_recorded_text(required_table(table, "source_locator"));
    result.format_version = parse_recorded_text(required_table(table, "format_version"));
    result.identified = parse_recorded_identity(required_table(table, "identified"));
    result.consumed = parse_recorded_identity(required_table(table, "consumed"));
    result.stability_provenance = parse_provenance(required_table(table, "stability_provenance"));
    return result;
}

std::variant<std::string, ReferenceStoreError> encode_reference(const Reference &reference) {
    if (!well_formed(reference)) {
        return ReferenceStoreError::invalid_reference;
    }
    try {
        toml::array materials;
        for (const auto &material : reference.materials) {
            materials.push_back(material_table(material));
        }
        toml::array conditions;
        for (const auto &condition : reference.conditions) {
            conditions.push_back(toml::table{
                {"key", condition.key},
                {"recorded", recorded_text(condition.value)},
            });
        }
        toml::array differences;
        for (const auto &difference : reference.declared_differences) {
            differences.push_back(recorded_text(difference));
        }
        const toml::table document{
            {"schema_version", 1},
            {"schema_minor", 0},
            {"kind", "reference"},
            {"baseline_id", reference.baseline_id},
            {"execution_reference_id", reference.execution_reference_id},
            {"role", enum_name(reference.role, reference_role_names)},
            {"engine", build_table(reference.engine)},
            {"runtime", build_table(reference.runtime)},
            {"conditions_manifest_id", recorded_text(reference.conditions_manifest_id)},
            {"materials", std::move(materials)},
            {"conditions", std::move(conditions)},
            {"declared_differences", std::move(differences)},
        };
        std::ostringstream stream;
        stream << document;
        auto text = stream.str();
        if (text.size() > max_metadata_bytes) {
            return ReferenceStoreError::document_too_large;
        }
        return text;
    } catch (const ReferenceStoreError error) {
        return error;
    }
}

std::variant<Reference, ReferenceStoreError> decode_reference(const std::string_view text) {
    if (text.size() > max_metadata_bytes) {
        return ReferenceStoreError::document_too_large;
    }
    try {
        const auto document = toml::parse(text);
        const auto version = document["schema_version"].value<std::int64_t>();
        const auto minor = document["schema_minor"].value<std::int64_t>();
        const auto kind = document["kind"].value<std::string>();
        if (!version || !minor || *version != 1 || *minor != 0) {
            return ReferenceStoreError::incompatible_version;
        }
        if (!kind || *kind != "reference") {
            return ReferenceStoreError::invalid_document;
        }

        Reference result;
        result.baseline_id = required_text(document, "baseline_id");
        result.execution_reference_id = required_text(document, "execution_reference_id");
        result.role = parse_enum<ReferenceRole>(document, "role", reference_role_names);
        result.engine = parse_build(required_table(document, "engine"));
        result.runtime = parse_build(required_table(document, "runtime"));
        result.conditions_manifest_id =
            parse_recorded_text(required_table(document, "conditions_manifest_id"));

        const auto *materials = document["materials"].as_array();
        const auto *conditions = document["conditions"].as_array();
        const auto *differences = document["declared_differences"].as_array();
        if (materials == nullptr || conditions == nullptr || differences == nullptr ||
            materials->size() > max_reference_materials ||
            conditions->size() > max_reference_conditions ||
            differences->size() > max_reference_differences) {
            return ReferenceStoreError::invalid_document;
        }
        for (const auto &entry : *materials) {
            const auto *table = entry.as_table();
            if (table == nullptr) {
                return ReferenceStoreError::invalid_document;
            }
            result.materials.push_back(parse_material(*table));
        }
        for (const auto &entry : *conditions) {
            const auto *table = entry.as_table();
            if (table == nullptr) {
                return ReferenceStoreError::invalid_document;
            }
            result.conditions.push_back({required_text(*table, "key"),
                                         parse_recorded_text(required_table(*table, "recorded"))});
        }
        for (const auto &entry : *differences) {
            const auto *table = entry.as_table();
            if (table == nullptr) {
                return ReferenceStoreError::invalid_document;
            }
            result.declared_differences.push_back(parse_recorded_text(*table));
        }
        return well_formed(result) ? std::variant<Reference, ReferenceStoreError>{std::move(result)}
                                   : std::variant<Reference, ReferenceStoreError>{
                                         ReferenceStoreError::invalid_document};
    } catch (const toml::parse_error &) {
        return ReferenceStoreError::malformed_document;
    } catch (const ReferenceStoreError error) {
        return error;
    }
}

std::span<const std::byte> bytes(const std::string_view text) noexcept {
    return {reinterpret_cast<const std::byte *>(text.data()), text.size()};
}

ExclusiveWriteResult write_exclusive(const std::filesystem::path &path,
                                     const std::string_view text) noexcept {
#ifdef _WIN32
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS
                   ? ExclusiveWriteResult::already_exists
                   : ExclusiveWriteResult::io_error;
    }
    std::size_t offset{};
    bool written = true;
    while (offset < text.size()) {
        DWORD count{};
        const auto remaining = static_cast<DWORD>(text.size() - offset);
        if (!WriteFile(handle, text.data() + offset, remaining, &count, nullptr) || count == 0) {
            written = false;
            break;
        }
        offset += count;
    }
    if (!CloseHandle(handle)) {
        written = false;
    }
#else
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0) {
        return errno == EEXIST ? ExclusiveWriteResult::already_exists
                               : ExclusiveWriteResult::io_error;
    }
    std::size_t offset{};
    bool written = true;
    while (offset < text.size()) {
        const auto count = ::write(descriptor, text.data() + offset, text.size() - offset);
        if (count <= 0) {
            written = false;
            break;
        }
        offset += static_cast<std::size_t>(count);
    }
    if (::close(descriptor) != 0) {
        written = false;
    }
#endif
    if (!written) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return ExclusiveWriteResult::io_error;
    }
    return ExclusiveWriteResult::written;
}

} // namespace

ReferenceStoreResult write_immutable_reference(const ExclusiveEvidenceDirectory &directory,
                                               const Reference &reference) noexcept {
    try {
        const auto encoded = encode_reference(reference);
        const auto *text = std::get_if<std::string>(&encoded);
        if (text == nullptr) {
            return std::get<ReferenceStoreError>(encoded);
        }
        const auto path = directory.path() / "reference.toml";
        switch (write_exclusive(path, *text)) {
        case ExclusiveWriteResult::already_exists:
            return ReferenceStoreError::already_exists;
        case ExclusiveWriteResult::io_error:
            return ReferenceStoreError::io_error;
        case ExclusiveWriteResult::written:
            return StoredReference{path, identify_content(bytes(*text)), reference};
        }
    } catch (...) {
        return ReferenceStoreError::io_error;
    }
    return ReferenceStoreError::io_error;
}

ReferenceStoreResult read_reference(const std::filesystem::path &path) noexcept {
    try {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) {
            return ReferenceStoreError::io_error;
        }
        if (size > max_metadata_bytes) {
            return ReferenceStoreError::document_too_large;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return ReferenceStoreError::io_error;
        }
        const std::string text{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
        if (input.bad() || text.size() != size) {
            return ReferenceStoreError::io_error;
        }
        const auto decoded = decode_reference(text);
        const auto *reference = std::get_if<Reference>(&decoded);
        if (reference == nullptr) {
            return std::get<ReferenceStoreError>(decoded);
        }
        return StoredReference{path, identify_content(bytes(text)), *reference};
    } catch (...) {
        return ReferenceStoreError::io_error;
    }
}

} // namespace ayther::audio_qa
