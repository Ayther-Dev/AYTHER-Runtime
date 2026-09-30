#include "checkpoint_store.h"

#include "content_hash.h"
#include "fact_fragment_store.h"
#include "model_limits.h"
#include "pcm_block_store.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <toml++/toml.hpp>
#include <utility>
#include <vector>

namespace ayther::audio_qa {

struct CheckpointStoreFactory {
    static StoredCheckpoint create(std::filesystem::path path,
                                   const ContentIdentity document_identity,
                                   EvidenceCheckpoint checkpoint) {
        return StoredCheckpoint(std::move(path), document_identity, std::move(checkpoint));
    }
};

namespace {

constexpr std::array kind_names{"fact_fragment", "pcm_block"};
constexpr char hex_digits[] = "0123456789abcdef";

bool identifier(const std::string_view value) noexcept {
    if (value.empty() || value.size() > max_identity_bytes || value == "." || value == "..") {
        return false;
    }
    for (const unsigned char character : value) {
        if (!(character >= 'a' && character <= 'z') && !(character >= 'A' && character <= 'Z') &&
            !(character >= '0' && character <= '9') && character != '-' && character != '_' &&
            character != '.') {
            return false;
        }
    }
    return true;
}

std::string hex(const std::array<std::uint8_t, 32> &bytes) {
    std::string result(bytes.size() * 2, '0');
    for (std::size_t index{}; index < bytes.size(); ++index) {
        result[index * 2] = hex_digits[bytes[index] >> 4U];
        result[index * 2 + 1] = hex_digits[bytes[index] & 0x0fU];
    }
    return result;
}

std::optional<std::array<std::uint8_t, 32>> parse_hex(const std::string_view value) noexcept {
    if (value.size() != 64) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 32> result{};
    for (std::size_t index{}; index < result.size(); ++index) {
        unsigned parsed_value{};
        const auto parsed = std::from_chars(value.data() + index * 2, value.data() + index * 2 + 2,
                                            parsed_value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + index * 2 + 2 ||
            parsed_value > 255U) {
            return std::nullopt;
        }
        result[index] = static_cast<std::uint8_t>(parsed_value);
    }
    return result;
}

std::optional<std::uint64_t> decimal(const std::string_view value) noexcept {
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

std::span<const std::byte> bytes(const std::string_view text) noexcept {
    return {reinterpret_cast<const std::byte *>(text.data()), text.size()};
}

const char *kind_name(const CheckpointArtifactKind kind) {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= kind_names.size()) {
        throw CheckpointStoreError::invalid_checkpoint;
    }
    return kind_names[index];
}

std::optional<CheckpointArtifactKind> kind_value(const std::string_view value) {
    for (std::size_t index{}; index < kind_names.size(); ++index) {
        if (value == kind_names[index]) {
            return static_cast<CheckpointArtifactKind>(index);
        }
    }
    return std::nullopt;
}

bool safe_relative_path(const std::filesystem::path &path) noexcept {
    if (path.empty() || path.is_absolute() || path.has_root_path() ||
        path.lexically_normal() != path) {
        return false;
    }
    for (const auto &component : path) {
        if (component == "." || component == "..") {
            return false;
        }
    }
    return true;
}

std::variant<CheckpointArtifact, CheckpointStoreError>
validate_artifact(const std::filesystem::path &run_directory, const CheckpointArtifactKind kind,
                  const std::uint64_t sequence, const DurablePublishedFile &publication) {
    if (sequence == 0) {
        return CheckpointStoreError::artifact_invalid;
    }
    const auto relative = publication.path().lexically_relative(run_directory);
    if (!safe_relative_path(relative)) {
        return CheckpointStoreError::artifact_outside_run;
    }
    const auto expected_parent =
        kind == CheckpointArtifactKind::fact_fragment ? "fragments" : "audio";
    if (relative.parent_path() != expected_parent) {
        return CheckpointStoreError::artifact_invalid;
    }
    ContentIdentity observed;
    if (kind == CheckpointArtifactKind::fact_fragment) {
        const auto read = read_fact_fragment(publication.path());
        const auto *fragment = std::get_if<StoredFactFragment>(&read);
        if (fragment == nullptr || fragment->sequence != sequence) {
            return CheckpointStoreError::artifact_invalid;
        }
        observed = fragment->document_identity;
    } else {
        const auto read = read_pcm_block(publication.path());
        const auto *block = std::get_if<StoredPcmBlock>(&read);
        if (block == nullptr || block->sequence != sequence) {
            return CheckpointStoreError::artifact_invalid;
        }
        observed = block->document_identity;
    }
    if (observed != publication.identity()) {
        return CheckpointStoreError::artifact_changed;
    }
    return CheckpointArtifact{kind, relative.generic_string(), sequence, observed};
}

std::variant<std::string, CheckpointStoreError>
encode_checkpoint(const EvidenceCheckpoint &checkpoint) {
    try {
        if (!identifier(checkpoint.checkpoint_id) || checkpoint.sequence == 0 ||
            checkpoint.artifacts.empty() ||
            checkpoint.artifacts.size() > max_checkpoint_artifacts) {
            return CheckpointStoreError::invalid_checkpoint;
        }
        toml::array artifacts;
        std::set<std::pair<CheckpointArtifactKind, std::uint64_t>> identities;
        std::set<std::string> paths;
        for (const auto &artifact : checkpoint.artifacts) {
            if (artifact.sequence == 0 || !safe_relative_path(artifact.relative_path) ||
                !identities.emplace(artifact.kind, artifact.sequence).second ||
                !paths.emplace(artifact.relative_path).second) {
                return CheckpointStoreError::invalid_checkpoint;
            }
            artifacts.push_back(
                toml::table{{"kind", kind_name(artifact.kind)},
                            {"relative_path", artifact.relative_path},
                            {"sequence", std::to_string(artifact.sequence)},
                            {"sha256", hex(artifact.identity.sha256)},
                            {"byte_size", std::to_string(artifact.identity.byte_size)}});
        }
        toml::table document{{"schema_version", 1},
                             {"schema_minor", 0},
                             {"checkpoint_id", checkpoint.checkpoint_id},
                             {"sequence", std::to_string(checkpoint.sequence)},
                             {"artifacts", std::move(artifacts)}};
        std::ostringstream stream;
        stream << document;
        auto text = stream.str();
        if (text.empty() || text.size() > max_metadata_bytes) {
            return CheckpointStoreError::document_too_large;
        }
        return text;
    } catch (const CheckpointStoreError error) {
        return error;
    }
}

std::variant<EvidenceCheckpoint, CheckpointStoreError>
decode_checkpoint(const std::string_view text) {
    try {
        const auto table = toml::parse(text);
        if (table["schema_version"].value<std::int64_t>() != 1 ||
            table["schema_minor"].value<std::int64_t>() != 0) {
            return CheckpointStoreError::incompatible_version;
        }
        if (table.size() != 5) {
            return CheckpointStoreError::malformed_document;
        }
        const auto id = table["checkpoint_id"].value<std::string>();
        const auto sequence_text = table["sequence"].value<std::string>();
        const auto *artifact_array = table["artifacts"].as_array();
        if (!id || !sequence_text || !artifact_array || !identifier(*id) ||
            artifact_array->empty() || artifact_array->size() > max_checkpoint_artifacts) {
            return CheckpointStoreError::malformed_document;
        }
        const auto sequence = decimal(*sequence_text);
        if (!sequence || *sequence == 0) {
            return CheckpointStoreError::malformed_document;
        }
        EvidenceCheckpoint checkpoint{*id, *sequence, {}};
        std::set<std::pair<CheckpointArtifactKind, std::uint64_t>> identities;
        std::set<std::string> paths;
        checkpoint.artifacts.reserve(artifact_array->size());
        for (const auto &node : *artifact_array) {
            const auto *artifact = node.as_table();
            const auto kind_text =
                artifact ? (*artifact)["kind"].value<std::string>() : std::nullopt;
            const auto path =
                artifact ? (*artifact)["relative_path"].value<std::string>() : std::nullopt;
            const auto artifact_sequence_text =
                artifact ? (*artifact)["sequence"].value<std::string>() : std::nullopt;
            const auto hash = artifact ? (*artifact)["sha256"].value<std::string>() : std::nullopt;
            const auto size_text =
                artifact ? (*artifact)["byte_size"].value<std::string>() : std::nullopt;
            if (!artifact || artifact->size() != 5 || !kind_text || !path ||
                !artifact_sequence_text || !hash || !size_text) {
                return CheckpointStoreError::malformed_document;
            }
            const auto kind = kind_value(*kind_text);
            const auto artifact_sequence = decimal(*artifact_sequence_text);
            const auto digest = parse_hex(*hash);
            const auto size = decimal(*size_text);
            const std::filesystem::path relative{*path};
            if (!kind || !artifact_sequence || *artifact_sequence == 0 || !digest || !size ||
                !safe_relative_path(relative) || relative.generic_string() != *path ||
                !identities.emplace(*kind, *artifact_sequence).second ||
                !paths.emplace(*path).second) {
                return CheckpointStoreError::malformed_document;
            }
            checkpoint.artifacts.push_back({*kind, *path, *artifact_sequence, {*digest, *size}});
        }
        return checkpoint;
    } catch (const toml::parse_error &) {
        return CheckpointStoreError::malformed_document;
    }
}

bool ensure_checkpoint_directory(const std::filesystem::path &path) noexcept {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (!error && std::filesystem::exists(status)) {
        return std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status);
    }
    error.clear();
    return std::filesystem::create_directory(path, error) && !error;
}

std::optional<CheckpointStoreError>
verify_stored_artifact(const std::filesystem::path &run_directory,
                       const CheckpointArtifact &artifact) {
    const auto artifact_path = run_directory / artifact.relative_path;
    ContentIdentity observed;
    if (artifact.kind == CheckpointArtifactKind::fact_fragment) {
        const auto read = read_fact_fragment(artifact_path);
        const auto *fragment = std::get_if<StoredFactFragment>(&read);
        if (fragment == nullptr || fragment->sequence != artifact.sequence) {
            return CheckpointStoreError::artifact_invalid;
        }
        observed = fragment->document_identity;
    } else {
        const auto read = read_pcm_block(artifact_path);
        const auto *block = std::get_if<StoredPcmBlock>(&read);
        if (block == nullptr || block->sequence != artifact.sequence) {
            return CheckpointStoreError::artifact_invalid;
        }
        observed = block->document_identity;
    }
    return observed == artifact.identity
               ? std::nullopt
               : std::optional<CheckpointStoreError>{CheckpointStoreError::artifact_changed};
}

} // namespace

CheckpointStoreResult write_checkpoint(const ExclusiveEvidenceDirectory &directory,
                                       std::string checkpoint_id, const std::uint64_t sequence,
                                       const std::vector<DurableArtifactPublication> &artifacts,
                                       const DurablePublicationLimits limits) noexcept {
    try {
        if (!identifier(checkpoint_id) || sequence == 0 || artifacts.empty() ||
            artifacts.size() > max_checkpoint_artifacts) {
            return CheckpointStoreError::invalid_checkpoint;
        }
        EvidenceCheckpoint checkpoint{std::move(checkpoint_id), sequence, {}};
        checkpoint.artifacts.reserve(artifacts.size());
        for (const auto &artifact : artifacts) {
            const auto validated = validate_artifact(directory.path(), artifact.kind,
                                                     artifact.sequence, artifact.publication);
            const auto *value = std::get_if<CheckpointArtifact>(&validated);
            if (value == nullptr) {
                return std::get<CheckpointStoreError>(validated);
            }
            checkpoint.artifacts.push_back(*value);
        }
        const auto encoded = encode_checkpoint(checkpoint);
        const auto *text = std::get_if<std::string>(&encoded);
        if (text == nullptr) {
            return std::get<CheckpointStoreError>(encoded);
        }
        const auto checkpoint_directory = directory.path() / "checkpoints";
        if (!ensure_checkpoint_directory(checkpoint_directory)) {
            return CheckpointStoreError::io_error;
        }
        const auto path = checkpoint_directory / "current.toml";
        std::error_code exists_error;
        const bool checkpoint_exists = std::filesystem::exists(path, exists_error);
        if (exists_error) {
            return CheckpointStoreError::io_error;
        }
        if (checkpoint_exists) {
            const auto previous = read_checkpoint(path);
            const auto *stored = std::get_if<StoredCheckpoint>(&previous);
            if (stored == nullptr) {
                return CheckpointStoreError::existing_checkpoint_invalid;
            }
            if (sequence <= stored->checkpoint().sequence) {
                return CheckpointStoreError::non_increasing_sequence;
            }
        }
        const auto published = publish_durable_file(path, bytes(*text), limits);
        const auto *file = std::get_if<DurablePublishedFile>(&published);
        if (file == nullptr) {
            return CheckpointStoreError::publish_failed;
        }
        return CheckpointStoreFactory::create(path, file->identity(), checkpoint);
    } catch (...) {
        return CheckpointStoreError::io_error;
    }
}

CheckpointStoreResult read_checkpoint(const std::filesystem::path &path) noexcept {
    const auto audited = audit_checkpoint_artifacts(path);
    const auto *audit = std::get_if<CheckpointArtifactAudit>(&audited);
    if (audit == nullptr) {
        return std::get<CheckpointStoreError>(audited);
    }
    if (audit->first_unverifiable) {
        return audit->first_unverifiable->reason;
    }
    return CheckpointStoreFactory::create(audit->path, audit->document_identity, audit->checkpoint);
}

CheckpointArtifactAuditResult
audit_checkpoint_artifacts(const std::filesystem::path &path) noexcept {
    try {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) {
            return CheckpointStoreError::io_error;
        }
        if (size > max_metadata_bytes) {
            return CheckpointStoreError::document_too_large;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return CheckpointStoreError::io_error;
        }
        const std::string text{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
        if (input.bad() || text.size() != size) {
            return CheckpointStoreError::io_error;
        }
        const auto decoded = decode_checkpoint(text);
        const auto *checkpoint = std::get_if<EvidenceCheckpoint>(&decoded);
        if (checkpoint == nullptr) {
            return std::get<CheckpointStoreError>(decoded);
        }
        CheckpointArtifactAudit audit{
            path, identify_content(bytes(text)), *checkpoint, {}, std::nullopt};
        audit.verified_artifacts.reserve(checkpoint->artifacts.size());
        const auto run_directory = path.parent_path().parent_path();
        for (const auto &artifact : checkpoint->artifacts) {
            const auto failure = verify_stored_artifact(run_directory, artifact);
            if (failure) {
                audit.first_unverifiable = UnverifiableCheckpointArtifact{artifact, *failure};
                break;
            }
            audit.verified_artifacts.push_back(artifact);
        }
        return audit;
    } catch (...) {
        return CheckpointStoreError::io_error;
    }
}

} // namespace ayther::audio_qa
