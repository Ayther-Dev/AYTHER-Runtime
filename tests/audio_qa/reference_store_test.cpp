#include "content_hash.h"
#include "exclusive_evidence_directory.h"
#include "reference_store.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <variant>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

qa::ContentIdentity identity(const std::uint8_t marker, const std::uint64_t size) {
    qa::ContentIdentity value;
    value.sha256[0] = marker;
    value.sha256[31] = static_cast<std::uint8_t>(marker + 1);
    value.byte_size = size;
    return value;
}

qa::Reference reference() {
    const qa::Provenance manifest{qa::Origin::artifact_manifest, "initial-manifest"};
    const qa::Provenance measurement{qa::Origin::content_measurement, "initial-hash"};
    qa::Material pack;
    pack.material_id = "pack";
    pack.role = qa::MaterialRole::pack;
    pack.source_locator = {"C:/private/Golden Axe.ay", manifest};
    pack.format_version = {"AYPK-1", manifest};
    pack.identified = {identity(0x11, 8192), measurement};

    qa::Reference value;
    value.baseline_id = "play-ce-baseline";
    value.execution_reference_id = "reference-initial";
    value.role = qa::ReferenceRole::initial;
    value.engine.artifact = {identity(0x31, 65536), measurement};
    value.engine.release = {"v0.1.0-rc.9", manifest};
    value.runtime.commit = {std::nullopt,
                            {qa::Origin::runtime_observation, "runtime-commit-unavailable"}};
    value.conditions_manifest_id = {"conditions-initial", manifest};
    value.conditions.push_back({"profile", {"full", manifest}});
    value.materials.push_back(pack);
    value.declared_differences.push_back(
        {"qa observation enabled", {qa::Origin::user_declaration, "scope"}});
    return value;
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "reference_file_open_failed");
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void remove_tree(const std::filesystem::path &path) {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

const qa::ExclusiveEvidenceDirectory &
directory(const qa::ExclusiveEvidenceDirectoryResult &result) {
    const auto *value = std::get_if<qa::ExclusiveEvidenceDirectory>(&result);
    require(value != nullptr, "evidence_directory_reservation_failed");
    return *value;
}

const qa::StoredReference &stored(const qa::ReferenceStoreResult &result,
                                  const char *const message) {
    const auto *value = std::get_if<qa::StoredReference>(&result);
    require(value != nullptr, message);
    return *value;
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-137-reference";
    remove_tree(fixture);
    try {
        const auto first_directory =
            qa::create_exclusive_evidence_directory(fixture, "run-initial");
        const auto second_directory = qa::create_exclusive_evidence_directory(fixture, "run-later");
        const auto initial = reference();

        const auto written = qa::write_immutable_reference(directory(first_directory), initial);
        const auto &initial_file = stored(written, "initial_reference_was_not_written");
        require(initial_file.path == directory(first_directory).path() / "reference.toml" &&
                    initial_file.reference == initial &&
                    std::filesystem::is_regular_file(initial_file.path),
                "written_reference_metadata_was_incomplete");
        const auto original_bytes = read_text(initial_file.path);

        const auto reopened = qa::read_reference(initial_file.path);
        const auto &reopened_initial = stored(reopened, "initial_reference_was_not_reopened");
        require(reopened_initial.reference == initial &&
                    reopened_initial.document_identity == initial_file.document_identity &&
                    reopened_initial.reference.materials.front().identified.provenance ==
                        initial.materials.front().identified.provenance &&
                    reopened_initial.reference.engine.artifact.value ==
                        initial.engine.artifact.value,
                "reopened_reference_lost_hash_or_provenance");

        auto later = initial;
        later.role = qa::ReferenceRole::extended;
        later.execution_reference_id = "reference-later";
        later.materials.front().identified = {identity(0x22, 8192),
                                              {qa::Origin::content_measurement, "later-pack-hash"}};

        const auto overwrite = qa::write_immutable_reference(directory(first_directory), later);
        require(std::get_if<qa::ReferenceStoreError>(&overwrite) != nullptr &&
                    std::get<qa::ReferenceStoreError>(overwrite) ==
                        qa::ReferenceStoreError::already_exists &&
                    read_text(initial_file.path) == original_bytes,
                "later_reference_overwrote_initial_metadata");

        const auto later_written =
            qa::write_immutable_reference(directory(second_directory), later);
        const auto &later_file =
            stored(later_written, "later_reference_was_not_written_separately");
        const auto later_reopened = qa::read_reference(later_file.path);
        require(stored(later_reopened, "later_reference_was_not_reopened").reference == later &&
                    later_file.document_identity != initial_file.document_identity &&
                    stored(qa::read_reference(initial_file.path),
                           "initial_reference_was_lost_after_later_write")
                            .reference == initial,
                "later_reference_was_not_distinguished_from_initial");

        remove_tree(fixture);
        std::puts("reference_store_test: passed");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "reference_store_test: %s\n", error.what());
        return 1;
    }
}
