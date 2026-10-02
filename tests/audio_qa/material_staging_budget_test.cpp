#include "content_hash.h"
#include "material_staging_budget.h"
#include "pinned_pack.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::span<const std::byte> bytes(const std::string_view text) {
    return {reinterpret_cast<const std::byte *>(text.data()), text.size()};
}

void write(const std::filesystem::path &path, const std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "material_budget_fixture_open_failed");
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    require(static_cast<bool>(output), "material_budget_fixture_write_failed");
}

void remove_tree(const std::filesystem::path &path) {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

qa::MaterialStagingPermit permit(const qa::MaterialStagingCheckResult &result,
                                 const char *const message) {
    const auto *value = std::get_if<qa::MaterialStagingPermit>(&result);
    require(value != nullptr, message);
    return *value;
}

qa::MaterialStagingRejection rejection(const qa::MaterialStagingCheckResult &result,
                                       const qa::MaterialStagingRejectionReason reason,
                                       const char *const message) {
    const auto *value = std::get_if<qa::MaterialStagingRejection>(&result);
    require(value != nullptr && value->reason == reason &&
                value->evidence_result == qa::EvidenceResult::incomplete &&
                qa::well_formed(value->diagnostic),
            message);
    return *value;
}

} // namespace

int main() {
    const auto fixture = std::filesystem::current_path() / "qa-187-material-budget";
    remove_tree(fixture);
    try {
        constexpr auto gib = std::uint64_t{1} << 30U;
        const std::vector<qa::MaterialStagingItem> maximum{
            {"pack", 4 * gib, true, false},
            {"rom-and-core", 4 * gib, true, false},
            {"unselected-library-entry", 100 * gib, false, false}};
        const auto exact = qa::check_material_staging_budget("run-187", maximum, 20 * gib);
        const auto exact_permit = permit(exact, "exact_material_and_reserve_limit_was_rejected");
        require(exact_permit.selected_bytes == 8 * gib &&
                    exact_permit.bytes_to_allocate == 8 * gib &&
                    exact_permit.required_free_bytes == 20 * gib,
                "unselected_material_was_counted_or_reserve_was_incorrect");

        const auto &missing_reserve =
            rejection(qa::check_material_staging_budget("run-187", maximum, 20 * gib - 1),
                      qa::MaterialStagingRejectionReason::storage_reserve_unavailable,
                      "missing_material_reserve_was_not_incomplete");
        require(missing_reserve.diagnostic.code == "material_storage_reserve_unavailable" &&
                    missing_reserve.required_free_bytes == 20 * gib,
                "material_reserve_diagnostic_lost_boundary");

        auto excessive = maximum;
        excessive[1].byte_size += 1;
        const auto &too_large =
            rejection(qa::check_material_staging_budget("run-187", excessive, 21 * gib),
                      qa::MaterialStagingRejectionReason::private_material_limit_exceeded,
                      "private_material_excess_was_not_incomplete");
        require(too_large.selected_bytes == 8 * gib + 1 &&
                    too_large.diagnostic.code == "private_material_limit_exceeded",
                "private_material_limit_diagnostic_lost_boundary");

        const std::vector<qa::MaterialStagingItem> partly_staged{
            {"pack", 3 * gib, true, true},
            {"game", 5 * gib, true, false},
            {"unrelated", 50 * gib, false, false}};
        const auto staged_permit =
            permit(qa::check_material_staging_budget("run-187", partly_staged, 17 * gib),
                   "remaining_allocation_reserve_was_rejected");
        require(staged_permit.selected_bytes == 8 * gib &&
                    staged_permit.already_staged_bytes == 3 * gib &&
                    staged_permit.bytes_to_allocate == 5 * gib &&
                    staged_permit.required_free_bytes == 17 * gib,
                "already_staged_bytes_were_reserved_twice");

        const auto source = fixture / "source";
        const auto staging = fixture / "staged";
        const auto pack_path = source / "game.ay";
        const auto selected_path = source / "selected.wav";
        const auto unrelated_path = source / "unrelated-secret.bin";
        constexpr std::string_view pack_content = "AYPK-selected-pack";
        constexpr std::string_view selected_content = "RIFF-selected-resource";
        constexpr std::string_view unrelated_content = "must-not-be-copied";
        write(pack_path, pack_content);
        write(selected_path, selected_content);
        write(unrelated_path, unrelated_content);
        const std::vector<qa::ExternalPackResource> selected_resources{
            {"samples/selected.wav", selected_path, qa::identify_content(bytes(selected_content))}};
        {
            const auto staged = qa::stage_pinned_pack(
                pack_path, qa::identify_content(bytes(pack_content)), selected_resources, staging);
            const auto *pinned = std::get_if<qa::PinnedPack>(&staged);
            require(pinned != nullptr && pinned->external_resources().size() == 1 &&
                        pinned->resolve_external("samples/selected.wav") != nullptr &&
                        pinned->resolve_external("unrelated-secret.bin") == nullptr &&
                        !std::filesystem::exists(staging / "unrelated-secret.bin") &&
                        !std::filesystem::exists(staging / "resources" / "unrelated-secret.bin"),
                    "unselected_source_was_copied_into_private_staging");
        }
        require(std::filesystem::exists(unrelated_path) && !std::filesystem::exists(staging),
                "staging_cleanup_removed_unselected_source_or_left_private_data");

        remove_tree(fixture);
        std::puts("material_staging_budget_test: selected=8GiB "
                  "margin=2GiB evidence_reserve=10GiB unrelated=excluded");
        return 0;
    } catch (const std::exception &error) {
        remove_tree(fixture);
        std::fprintf(stderr, "material_staging_budget_test: %s\n", error.what());
        return 1;
    }
}
