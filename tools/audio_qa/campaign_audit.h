#pragma once

#include "fact_model.h"
#include "replay_execution_result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

enum class CampaignAuditError {
    invalid_input,
    evidence_unavailable,
    fact_integrity_failed,
    audio_integrity_failed,
};

struct CampaignAuditSummary {
    std::string run_id;
    std::uint64_t fact_fragments{};
    std::uint64_t facts{};
    std::uint64_t typed_fields{};
    std::uint64_t declared_assignments{};
    std::uint64_t parsed_assignments{};
    std::uint64_t loaded_assignments{};
    std::uint64_t selected_assignments{};
    std::uint64_t pending_assignments{};
    std::vector<std::string> pending_assignment_ids;
    std::uint64_t detector_inputs{};
    std::uint64_t detector_batches{};
    std::uint64_t candidates{};
    std::uint64_t selections{};
    std::uint64_t playback_requests{};
    std::uint64_t playback_decisions{};
    std::uint64_t playback_effects{};
    std::uint64_t mix_spans{};
    std::uint64_t position_spans{};
    std::uint64_t loop_crossings{};
    std::uint64_t voice_ends{};
    std::uint64_t reasoned_decisions{};
    std::uint64_t reasoned_ends{};
    std::uint64_t pcm_blocks{};
    std::uint64_t pcm_bytes{};
    std::uint64_t audio_begin{};
    std::uint64_t audio_end{};
    ReplayTraceSummary trace;
    FactId query_origin;
    FactId query_mix;
    std::string query_relation;
    std::uint64_t query_audio_begin{};
    std::uint64_t query_audio_end{};
    bool typed_payload_complete{};
    bool stage_relations_complete{};
    bool audio_complete{};
    bool query_route_complete{};
    bool complete{};
};

using CampaignAuditResult = std::variant<CampaignAuditSummary, CampaignAuditError>;

[[nodiscard]] CampaignAuditResult
audit_campaign_evidence(const std::filesystem::path &run_directory, std::string_view run_id,
                        std::uint64_t expected_assignments) noexcept;
[[nodiscard]] std::string format_campaign_audit(const CampaignAuditSummary &summary);
[[nodiscard]] std::string_view campaign_audit_error_code(CampaignAuditError error) noexcept;

} // namespace ayther::audio_qa
