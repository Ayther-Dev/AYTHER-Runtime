#include "campaign_audit.h"

#include "exclusive_evidence_directory.h"
#include "fact_fragment_store.h"
#include "pcm_block_store.h"
#include "pcm_message.h"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition)
        throw std::runtime_error{message};
}

qa::FactField number(std::string name, const std::uint64_t value,
                     const qa::FactFieldUnit unit = qa::FactFieldUnit::none) {
    return {std::move(name), qa::Availability::known, unit, value, {}};
}

qa::FactField text(std::string name, std::string value) {
    return {
        std::move(name), qa::Availability::known, qa::FactFieldUnit::none, std::move(value), {}};
}

qa::FactField boolean(std::string name, const bool value) {
    return {std::move(name), qa::Availability::known, qa::FactFieldUnit::none, value, {}};
}

qa::FactField not_applicable(std::string name, std::string reason) {
    return {std::move(name), qa::Availability::not_applicable, qa::FactFieldUnit::none,
            std::monostate{}, std::move(reason)};
}

qa::Fact fact(const std::uint32_t producer, const std::uint64_t sequence, std::string kind,
              std::vector<qa::Cause> causes = {}) {
    qa::Fact result;
    result.id = {"run-audit", "engine-" + std::to_string(producer), sequence};
    result.kind = std::move(kind);
    result.frame_index = {qa::Availability::known, 0, {}};
    result.cause_ids = std::move(causes);
    result.decision_id = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    result.assignment_id = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    result.occurrence_id = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    result.reason_code = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    result.shared_state_order = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    return result;
}

std::vector<qa::Fact> facts() {
    std::vector<qa::Fact> result;
    auto declared = fact(2, 1, "pack_assignment_declared");
    declared.fields = {number("ordinal", 0, qa::FactFieldUnit::count),
                       text("authored_signature", "77"), number("asset_bytes", 8)};
    result.push_back(std::move(declared));
    auto parsed =
        fact(2, 2, "pack_assignment_parse_result", {qa::FactId{"run-audit", "engine-2", 1}});
    parsed.reason_code = {qa::Availability::known, "accepted_by_parser", {}};
    parsed.fields = {boolean("accepted", true), text("reason", "accepted_by_parser")};
    result.push_back(std::move(parsed));
    auto loaded = fact(2, 3, "pack_assignment_loaded", {qa::FactId{"run-audit", "engine-2", 2}});
    loaded.fields = {number("signature", 77),
                     text("asset", "music.wav"),
                     text("stage", "session_assignment_map"),
                     boolean("accepted", true),
                     text("reason", "inserted"),
                     number("parsed_duration", 60),
                     number("parsed_span", 10),
                     boolean("parsed_looping", true)};
    result.push_back(std::move(loaded));

    result.push_back(fact(3, 1, "detector_input"));
    result.push_back(fact(3, 2, "detector_input_batch"));
    auto candidate = fact(5, 1, "assignment_candidate", {qa::FactId{"run-audit", "engine-3", 1}});
    candidate.fields = {number("signature", 77), text("rule", "exact"),
                        text("origin", "exact_lookup")};
    result.push_back(std::move(candidate));
    auto selection = fact(5, 2, "assignment_selection", {qa::FactId{"run-audit", "engine-5", 1}});
    selection.assignment_id = {qa::Availability::known, "77", {}};
    selection.fields = {text("result", "selected"), number("selected_signature", 77),
                        text("branch", "exact")};
    result.push_back(std::move(selection));
    auto request = fact(5, 3, "hd_playback_request", {qa::FactId{"run-audit", "engine-5", 2}});
    request.assignment_id = {qa::Availability::known, "77", {}};
    request.occurrence_id = {qa::Availability::known, "9", {}};
    request.fields = {number("selection", 2), boolean("links_complete", true)};
    result.push_back(std::move(request));
    auto decision = fact(5, 4, "hd_playback_decision", {qa::FactId{"run-audit", "engine-5", 3}});
    decision.occurrence_id = {qa::Availability::known, "9", {}};
    decision.reason_code = {qa::Availability::known, "same_key_same_asset", {}};
    decision.fields = {text("action", "restart"), text("reason", "same_key_same_asset"),
                       number("occurrence", 9), number("previous_occurrence", 8)};
    result.push_back(std::move(decision));
    auto effect = fact(5, 5, "hd_playback_effect", {qa::FactId{"run-audit", "engine-5", 4}});
    effect.occurrence_id = {qa::Availability::known, "9", {}};
    effect.fields = {text("result", "no_start"),
                     not_applicable("played", "no_physical_start_attempt"),
                     number("occurrence", 9)};
    result.push_back(std::move(effect));

    auto mix = fact(6, 1, "hd_mix_participant", {qa::FactId{"run-audit", "engine-5", 3}});
    mix.occurrence_id = {qa::Availability::known, "9", {}};
    mix.fields = {
        number("occurrence", 9),
        text("key", "music"),
        text("mix_timeline", "engine_main_mix"),
        number("mix_begin", 10, qa::FactFieldUnit::sample_frame),
        number("mix_end", 14, qa::FactFieldUnit::sample_frame),
        number("mix_sample_rate", 44100),
        text("track_timeline", "hd_asset_pcm"),
        number("track_begin", 20),
        number("track_end", 24),
        number("track_limit", 100),
        number("track_sample_rate", 44100),
        {"effective_gain_begin", qa::Availability::known, qa::FactFieldUnit::linear_gain, 1.0, {}},
        {"effective_gain_end", qa::Availability::known, qa::FactFieldUnit::linear_gain, 1.0, {}},
        boolean("muted_by_gain", false),
        boolean("nonzero_contribution", true),
        boolean("links_complete", true)};
    result.push_back(std::move(mix));
    auto position = fact(6, 2, "hd_voice_position_span", {qa::FactId{"run-audit", "engine-5", 3}});
    position.occurrence_id = {qa::Availability::known, "9", {}};
    position.fields = {
        number("occurrence", 9),     text("key", "music"),         number("output_begin", 10),
        number("output_end", 14),    number("source_begin", 20),   number("source_end", 24),
        number("source_limit", 100), number("sample_rate", 44100), boolean("links_complete", true)};
    result.push_back(std::move(position));
    auto previous_position =
        fact(6, 3, "hd_voice_position_span", {qa::FactId{"run-audit", "engine-5", 3}});
    previous_position.occurrence_id = {qa::Availability::known, "8", {}};
    previous_position.fields = {
        number("occurrence", 8),     text("key", "music"),         number("output_begin", 6),
        number("output_end", 10),    number("source_begin", 40),   number("source_end", 44),
        number("source_limit", 100), number("sample_rate", 44100), boolean("links_complete", true)};
    result.push_back(std::move(previous_position));
    auto previous_mix = fact(6, 4, "hd_mix_participant", {qa::FactId{"run-audit", "engine-5", 3}});
    previous_mix.occurrence_id = {qa::Availability::known, "8", {}};
    previous_mix.fields = {
        number("occurrence", 8),
        text("key", "music"),
        text("mix_timeline", "engine_main_mix"),
        number("mix_begin", 6, qa::FactFieldUnit::sample_frame),
        number("mix_end", 10, qa::FactFieldUnit::sample_frame),
        number("mix_sample_rate", 44100),
        text("track_timeline", "hd_asset_pcm"),
        number("track_begin", 40),
        number("track_end", 44),
        number("track_limit", 100),
        number("track_sample_rate", 44100),
        {"effective_gain_begin", qa::Availability::known, qa::FactFieldUnit::linear_gain, 1.0, {}},
        {"effective_gain_end", qa::Availability::known, qa::FactFieldUnit::linear_gain, 1.0, {}},
        boolean("muted_by_gain", false),
        boolean("nonzero_contribution", true),
        boolean("links_complete", true)};
    result.push_back(std::move(previous_mix));
    auto output_span =
        fact(8, 1, "main_mix_output_span",
             {qa::FactId{"run-audit", "engine-6", 1}, qa::FactId{"run-audit", "engine-7", 2}});
    output_span.fields = {number("input_begin", 10), number("input_end", 14),
                          number("output_begin", 10), number("output_end", 14)};
    result.push_back(std::move(output_span));
    return result;
}

qa::AudioChunk audio() {
    qa::AudioChunk chunk;
    chunk.run_id = "run-audit";
    chunk.capture_point = "sdl_logical_device_postmix";
    chunk.producer_sequence = 1;
    chunk.format = {qa::PcmFormat::f32le, 44100, 2};
    chunk.range = {"engine_main_output", 44100, 8, 16};
    chunk.bytes.resize(8U * 2U * sizeof(float));
    chunk.sha256 = {qa::Availability::known, qa::pcm_sha256(chunk.bytes), {}};
    chunk.checkpoint_id = {qa::Availability::not_applicable, std::nullopt, "not_published"};
    return chunk;
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "ayther-audio-qa-campaign-audit";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    try {
        const auto reserved = qa::create_exclusive_evidence_directory(root / "runs", "run-audit");
        const auto *directory = std::get_if<qa::ExclusiveEvidenceDirectory>(&reserved);
        require(directory != nullptr, "audit_fixture_directory_failed");
        require(std::holds_alternative<qa::StoredFactFragment>(
                    qa::write_fact_fragment(*directory, 1, facts())),
                "audit_fixture_facts_failed");
        require(
            std::holds_alternative<qa::StoredPcmBlock>(qa::write_pcm_block(*directory, 1, audio())),
            "audit_fixture_audio_failed");
        const auto audited = qa::audit_campaign_evidence(directory->path(), "run-audit", 1);
        const auto *summary = std::get_if<qa::CampaignAuditSummary>(&audited);
        require(
            summary != nullptr && summary->complete && summary->facts == 15U &&
                summary->typed_payload_complete && summary->invalid_typed_payloads == 0U &&
                summary->first_invalid_typed_payload_kind.empty() &&
                summary->stage_relations_complete && summary->audio_complete &&
                summary->query_route_complete && summary->main_output_spans == 1U &&
                summary->causal_output_links == 1U && summary->restart_position_complete == 1U &&
                summary->restart_mix_link_complete == 1U &&
                summary->restart_output_complete == 1U && summary->restart_chain_complete == 1U &&
                summary->selected_assignments == 1U && summary->pending_assignments == 0U &&
                summary->pending_assignment_ids.empty() && summary->query_audio_begin == 10U &&
                summary->query_audio_end == 14U && summary->restart_candidates.size() == 1U &&
                summary->restart_candidates.front().occurrence == 9U &&
                summary->restart_candidates.front().previous_occurrence == 8U &&
                summary->restart_candidates.front().previous_source_end == 44U &&
                summary->restart_candidates.front().current_source_begin == 20U &&
                summary->restart_candidates.front().output_begin == 10U &&
                summary->restart_candidates.front().output_end == 14U &&
                summary->restart_candidates.front().event ==
                    qa::FactId{"run-audit", "engine-3", 1} &&
                summary->restart_candidates.front().query ==
                    qa::FactId{"run-audit", "engine-3", 1} &&
                summary->restart_candidates.front().candidate ==
                    qa::FactId{"run-audit", "engine-5", 1} &&
                summary->restart_candidates.front().selection ==
                    qa::FactId{"run-audit", "engine-5", 2} &&
                summary->restart_candidates.front().request ==
                    qa::FactId{"run-audit", "engine-5", 3} &&
                summary->restart_candidates.front().mix == qa::FactId{"run-audit", "engine-6", 1} &&
                summary->restart_candidates.front().output_span ==
                    qa::FactId{"run-audit", "engine-8", 1} &&
                summary->restart_candidates.front().pcm_sequence == 2U &&
                summary->restart_candidates.front().pcm_begin == 8U &&
                summary->restart_candidates.front().pcm_end == 16U,
            "complete_campaign_was_not_audited");
        std::filesystem::remove_all(root, ignored);
        return 0;
    } catch (const std::exception &error) {
        std::filesystem::remove_all(root, ignored);
        std::fprintf(stderr, "campaign_audit_test: %s\n", error.what());
        return 1;
    }
}
