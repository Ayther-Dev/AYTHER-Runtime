#include "campaign_audit.h"

#include "audio_integrity.h"
#include "fact_fragment_store.h"
#include "fact_trace_summary.h"
#include "model_limits.h"
#include "pcm_block_store.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ayther::audio_qa {
namespace {

struct MixRange {
    FactId id;
    std::uint32_t sample_rate{};
    std::uint64_t begin{};
    std::uint64_t end{};
};

struct VoiceRange {
    std::uint64_t first_source{(std::numeric_limits<std::uint64_t>::max)()};
    std::uint64_t last_source{};
    std::uint64_t first_output{(std::numeric_limits<std::uint64_t>::max)()};
    std::uint64_t last_output{};
};

struct PendingRestart {
    FactId decision;
    FactId request;
    std::string reason;
    std::uint64_t occurrence{};
    std::uint64_t previous_occurrence{};
};

struct FactIdHash {
    std::size_t operator()(const FactId &id) const noexcept {
        const auto text_hash = std::hash<std::string>{};
        const auto number_hash = std::hash<std::uint64_t>{};
        return text_hash(id.run_id) ^ (text_hash(id.producer_id) << 1U) ^
               (number_hash(id.producer_sequence) << 2U);
    }
};

struct OutputLink {
    FactId output;
    FactId mix;
    std::uint64_t pcm_sequence{};
};

std::optional<FactId> fact_cause(const Fact &fact, const std::string_view producer = {}) {
    for (const auto &cause : fact.cause_ids) {
        const auto *id = std::get_if<FactId>(&cause);
        if (id != nullptr && (producer.empty() || id->producer_id == producer))
            return *id;
    }
    return std::nullopt;
}

bool identity(const std::string_view value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}

std::vector<std::filesystem::path> files(const std::filesystem::path &directory,
                                         const std::string_view extension) {
    std::vector<std::filesystem::path> result;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator{directory, error}, end;
         !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_regular_file(error) && iterator->path().extension() == extension)
            result.push_back(iterator->path());
    }
    if (error)
        return {};
    std::sort(result.begin(), result.end());
    return result;
}

const FactField *field(const Fact &fact, const std::string_view name) noexcept {
    const auto found = std::find_if(fact.fields.begin(), fact.fields.end(),
                                    [name](const FactField &field) { return field.name == name; });
    return found == fact.fields.end() ? nullptr : &*found;
}

const FactField *known_field(const Fact &fact, const std::string_view name) noexcept {
    const auto *result = field(fact, name);
    return result != nullptr && result->availability == Availability::known ? result : nullptr;
}

std::optional<std::uint64_t> number(const Fact &fact, const std::string_view name) noexcept {
    const auto *field = known_field(fact, name);
    if (field == nullptr)
        return std::nullopt;
    if (const auto *value = std::get_if<std::uint64_t>(&field->value))
        return *value;
    return std::nullopt;
}

std::optional<std::string_view> text(const Fact &fact, const std::string_view name) noexcept {
    const auto *field = known_field(fact, name);
    if (field == nullptr)
        return std::nullopt;
    if (const auto *value = std::get_if<std::string>(&field->value))
        return *value;
    return std::nullopt;
}

std::optional<bool> boolean(const Fact &fact, const std::string_view name) noexcept {
    const auto *field = known_field(fact, name);
    if (field == nullptr)
        return std::nullopt;
    if (const auto *value = std::get_if<bool>(&field->value))
        return *value;
    return std::nullopt;
}

bool has_fields(const Fact &fact, const std::initializer_list<std::string_view> names) noexcept {
    return std::all_of(names.begin(), names.end(), [&fact](const std::string_view name) {
        return known_field(fact, name) != nullptr;
    });
}

bool valid_mix(const Fact &fact, MixRange &range) noexcept {
    const auto begin = number(fact, "mix_begin");
    const auto end = number(fact, "mix_end");
    const auto rate = number(fact, "mix_sample_rate");
    const auto track_begin = number(fact, "track_begin");
    const auto track_end = number(fact, "track_end");
    const auto track_limit = number(fact, "track_limit");
    const auto linked = boolean(fact, "links_complete");
    if (!begin || !end || !rate || !track_begin || !track_end || !track_limit || !linked ||
        !*linked || *begin >= *end || *track_begin >= *track_end || *track_end > *track_limit ||
        *rate == 0U || *rate > 192000U ||
        !has_fields(fact, {"occurrence", "key", "mix_timeline", "track_timeline",
                           "track_sample_rate", "effective_gain_begin", "effective_gain_end",
                           "muted_by_gain", "nonzero_contribution"}))
        return false;
    range = {fact.id, static_cast<std::uint32_t>(*rate), *begin, *end};
    return true;
}

bool valid_position(const Fact &fact) noexcept {
    const auto begin = number(fact, "output_begin");
    const auto end = number(fact, "output_end");
    const auto source_begin = number(fact, "source_begin");
    const auto source_end = number(fact, "source_end");
    const auto source_limit = number(fact, "source_limit");
    const auto linked = boolean(fact, "links_complete");
    return begin && end && source_begin && source_end && source_limit && linked && *linked &&
           *begin < *end && *source_begin < *source_end && *source_end <= *source_limit &&
           has_fields(fact, {"occurrence", "key", "sample_rate"});
}

bool add_checked(std::uint64_t &target, const std::uint64_t value) noexcept {
    if (value > (std::numeric_limits<std::uint64_t>::max)() - target)
        return false;
    target += value;
    return true;
}

} // namespace

CampaignAuditResult audit_campaign_evidence(const std::filesystem::path &run_directory,
                                            const std::string_view run_id,
                                            const std::uint64_t expected_assignments) noexcept {
    if (!identity(run_id) || expected_assignments == 0U ||
        expected_assignments > max_reference_materials)
        return CampaignAuditError::invalid_input;
    try {
        const auto fragments = files(run_directory / "fragments", ".aqf");
        const auto audio = files(run_directory / "audio", ".aqp");
        if (fragments.empty() || audio.empty())
            return CampaignAuditError::evidence_unavailable;

        CampaignAuditSummary summary;
        summary.run_id = run_id;
        summary.typed_payload_complete = true;
        ReplayFactTraceAccumulator trace{std::string{run_id}};
        std::unordered_set<std::string> loaded_signatures;
        std::unordered_set<std::string> selected_signatures;
        std::unordered_set<std::string> request_occurrences;
        std::unordered_set<std::string> decision_occurrences;
        std::unordered_set<std::string> effect_occurrences;
        std::unordered_set<std::string> mix_occurrences;
        std::vector<MixRange> mix_ranges;
        std::unordered_map<std::uint64_t, VoiceRange> voice_ranges;
        std::unordered_map<FactId, FactId, FactIdHash> causal_parent;
        std::unordered_set<FactId, FactIdHash> candidate_ids;
        std::unordered_map<std::uint64_t, std::vector<FactId>> occurrence_mixes;
        std::vector<OutputLink> output_links;
        std::vector<PendingRestart> pending_restarts;
        std::uint64_t expected_fragment_sequence{1U};

        for (const auto &path : fragments) {
            const auto reopened = read_fact_fragment(path);
            const auto *fragment = std::get_if<StoredFactFragment>(&reopened);
            if (fragment == nullptr || fragment->sequence != expected_fragment_sequence)
                return CampaignAuditError::fact_integrity_failed;
            ++expected_fragment_sequence;
            ++summary.fact_fragments;
            for (const auto &fact : fragment->facts) {
                const bool payload_was_complete = summary.typed_payload_complete;
                if (fact.id.run_id != run_id || !trace.consume(fact) ||
                    !add_checked(summary.facts, 1U) ||
                    !add_checked(summary.typed_fields, fact.fields.size()))
                    return CampaignAuditError::fact_integrity_failed;
                if (fact.kind == "pack_assignment_declared") {
                    ++summary.declared_assignments;
                    summary.typed_payload_complete =
                        summary.typed_payload_complete &&
                        has_fields(fact, {"ordinal", "authored_signature", "asset_bytes"});
                } else if (fact.kind == "pack_assignment_parse_result") {
                    const auto accepted = boolean(fact, "accepted");
                    if (accepted && *accepted)
                        ++summary.parsed_assignments;
                    summary.typed_payload_complete = summary.typed_payload_complete && accepted &&
                                                     text(fact, "reason").has_value();
                } else if (fact.kind == "pack_assignment_loaded") {
                    const auto signature = number(fact, "signature");
                    if (signature)
                        loaded_signatures.insert(std::to_string(*signature));
                    summary.typed_payload_complete =
                        summary.typed_payload_complete && signature &&
                        has_fields(fact, {"asset", "stage", "accepted", "reason", "parsed_duration",
                                          "parsed_span", "parsed_looping"});
                } else if (fact.kind == "detector_input") {
                    ++summary.detector_inputs;
                } else if (fact.kind == "detector_input_batch") {
                    ++summary.detector_batches;
                } else if (fact.kind == "assignment_candidate") {
                    ++summary.candidates;
                    summary.typed_payload_complete =
                        summary.typed_payload_complete &&
                        has_fields(fact, {"signature", "rule", "origin"});
                    if (const auto parent = fact_cause(fact))
                        causal_parent.emplace(fact.id, *parent);
                    candidate_ids.insert(fact.id);
                } else if (fact.kind == "assignment_selection") {
                    ++summary.selections;
                    const auto result = text(fact, "result");
                    const auto signature = number(fact, "selected_signature");
                    if (result && *result == "selected" && signature && *signature != 0U)
                        selected_signatures.insert(std::to_string(*signature));
                    summary.typed_payload_complete = summary.typed_payload_complete && result &&
                                                     signature && text(fact, "branch");
                    for (const auto &cause : fact.cause_ids)
                        if (const auto *id = std::get_if<FactId>(&cause);
                            id != nullptr && candidate_ids.contains(*id)) {
                            causal_parent.emplace(fact.id, *id);
                            break;
                        }
                } else if (fact.kind == "assignment_query") {
                    if (const auto parent = fact_cause(fact))
                        causal_parent.emplace(fact.id, *parent);
                } else if (fact.kind == "hd_playback_request") {
                    ++summary.playback_requests;
                    if (fact.occurrence_id.value)
                        request_occurrences.insert(*fact.occurrence_id.value);
                    const bool assignment = fact.assignment_id.value.has_value();
                    const bool occurrence = fact.occurrence_id.value.has_value();
                    const bool selection = known_field(fact, "selection") != nullptr;
                    const bool links = known_field(fact, "links_complete") != nullptr;
                    if (summary.typed_payload_complete &&
                        !(assignment && occurrence && selection && links)) {
                        std::ostringstream detail;
                        detail << "assignment=" << assignment << ",occurrence=" << occurrence
                               << ",selection=" << selection << ",links=" << links;
                        summary.first_invalid_typed_payload_detail = detail.str();
                    }
                    summary.typed_payload_complete = summary.typed_payload_complete && assignment &&
                                                     occurrence && selection && links;
                    if (const auto parent = fact_cause(fact))
                        causal_parent.emplace(fact.id, *parent);
                } else if (fact.kind == "hd_playback_decision") {
                    ++summary.playback_decisions;
                    if (fact.occurrence_id.value)
                        decision_occurrences.insert(*fact.occurrence_id.value);
                    if (fact.reason_code.value)
                        ++summary.reasoned_decisions;
                    summary.typed_payload_complete =
                        summary.typed_payload_complete &&
                        has_fields(fact, {"action", "reason", "occurrence"});
                    const auto action = text(fact, "action");
                    const auto reason = text(fact, "reason");
                    const auto current = number(fact, "occurrence");
                    const auto previous = number(fact, "previous_occurrence");
                    if (action && *action == "restart" && reason && current && previous)
                        if (const auto request = fact_cause(fact))
                            pending_restarts.push_back(
                                {fact.id, *request, std::string{*reason}, *current, *previous});
                } else if (fact.kind == "hd_playback_effect") {
                    ++summary.playback_effects;
                    if (fact.occurrence_id.value)
                        effect_occurrences.insert(*fact.occurrence_id.value);
                    summary.typed_payload_complete = summary.typed_payload_complete &&
                                                     has_fields(fact, {"result", "occurrence"}) &&
                                                     field(fact, "played") != nullptr;
                } else if (fact.kind == "hd_mix_participant") {
                    ++summary.mix_spans;
                    if (fact.occurrence_id.value)
                        mix_occurrences.insert(*fact.occurrence_id.value);
                    MixRange range;
                    if (valid_mix(fact, range))
                        mix_ranges.push_back(std::move(range));
                    else
                        summary.typed_payload_complete = false;
                    if (const auto occurrence = number(fact, "occurrence"))
                        occurrence_mixes[*occurrence].push_back(fact.id);
                } else if (fact.kind == "hd_voice_position_span") {
                    ++summary.position_spans;
                    const bool valid = valid_position(fact);
                    summary.typed_payload_complete = summary.typed_payload_complete && valid;
                    const auto occurrence = number(fact, "occurrence");
                    const auto source_begin = number(fact, "source_begin");
                    const auto source_end = number(fact, "source_end");
                    const auto output_begin = number(fact, "output_begin");
                    const auto output_end = number(fact, "output_end");
                    if (valid && occurrence && source_begin && source_end && output_begin &&
                        output_end) {
                        auto &range = voice_ranges[*occurrence];
                        range.first_source = (std::min)(range.first_source, *source_begin);
                        range.last_source = (std::max)(range.last_source, *source_end);
                        range.first_output = (std::min)(range.first_output, *output_begin);
                        range.last_output = (std::max)(range.last_output, *output_end);
                    }
                } else if (fact.kind == "hd_voice_loop_crossing") {
                    ++summary.loop_crossings;
                    summary.typed_payload_complete =
                        summary.typed_payload_complete &&
                        has_fields(fact, {"occurrence", "key", "output_position", "source_before",
                                          "source_after", "loop_begin", "loop_end", "source_limit",
                                          "links_complete"});
                } else if (fact.kind == "hd_voice_end") {
                    ++summary.voice_ends;
                    if (fact.reason_code.value)
                        ++summary.reasoned_ends;
                    summary.typed_payload_complete =
                        summary.typed_payload_complete && fact.reason_code.value &&
                        has_fields(fact, {"occurrence", "key", "reason", "source_position",
                                          "source_limit", "links_complete"});
                } else if (fact.kind == "main_mix_output_span") {
                    ++summary.main_output_spans;
                    const auto pcm = fact_cause(fact, "engine-7");
                    if (pcm)
                        for (const auto &cause : fact.cause_ids)
                            if (const auto *id = std::get_if<FactId>(&cause);
                                id != nullptr && id->producer_id == "engine-6")
                                output_links.push_back({fact.id, *id, pcm->producer_sequence});
                }
                if (payload_was_complete && !summary.typed_payload_complete) {
                    summary.invalid_typed_payloads = 1U;
                    summary.first_invalid_typed_payload_kind = fact.kind;
                    summary.first_invalid_typed_payload_id = fact.id;
                }
            }
        }

        summary.trace = trace.summarize(true);
        summary.loaded_assignments = loaded_signatures.size();
        summary.selected_assignments = selected_signatures.size();
        for (const auto &signature : loaded_signatures)
            if (!selected_signatures.contains(signature))
                summary.pending_assignment_ids.push_back(signature);
        std::ranges::sort(summary.pending_assignment_ids);
        summary.pending_assignments = summary.pending_assignment_ids.size();
        const auto contains_all = [](const auto &superset, const auto &subset) {
            return std::all_of(subset.begin(), subset.end(),
                               [&superset](const auto &value) { return superset.contains(value); });
        };
        summary.stage_relations_complete =
            summary.trace.loss_free && summary.trace.causally_connected &&
            summary.declared_assignments == expected_assignments &&
            summary.parsed_assignments == expected_assignments &&
            summary.loaded_assignments == expected_assignments && summary.detector_inputs > 0U &&
            summary.detector_batches > 0U && summary.candidates > 0U && summary.selections > 0U &&
            summary.playback_requests > 0U &&
            summary.playback_requests == summary.playback_decisions &&
            summary.playback_requests == summary.playback_effects && summary.mix_spans > 0U &&
            summary.position_spans == summary.mix_spans &&
            summary.reasoned_decisions == summary.playback_decisions &&
            summary.reasoned_ends == summary.voice_ends &&
            contains_all(decision_occurrences, request_occurrences) &&
            contains_all(effect_occurrences, request_occurrences) &&
            contains_all(mix_occurrences, request_occurrences) &&
            contains_all(loaded_signatures, selected_signatures);

        const auto audio_audit = audit_audio_integrity(audio);
        if (audio_audit.evidence_result != EvidenceResult::complete ||
            !audio_audit.last_verified_sample)
            return CampaignAuditError::audio_integrity_failed;
        std::vector<SampleFrameRange> audio_ranges;
        std::vector<std::pair<std::uint64_t, SampleFrameRange>> pcm_identity_ranges;
        audio_ranges.reserve(audio.size());
        for (const auto &path : audio) {
            const auto reopened = read_pcm_block(path);
            const auto *block = std::get_if<StoredPcmBlock>(&reopened);
            if (block == nullptr || block->chunk.run_id != run_id ||
                !add_checked(summary.pcm_bytes, block->chunk.bytes.size()))
                return CampaignAuditError::audio_integrity_failed;
            audio_ranges.push_back(block->chunk.range);
            pcm_identity_ranges.emplace_back(block->chunk.producer_sequence, block->chunk.range);
            if (summary.first_pcm_identity == 0U)
                summary.first_pcm_identity = block->chunk.producer_sequence;
            summary.last_pcm_identity = block->chunk.producer_sequence;
        }
        summary.pcm_blocks = audio.size();
        summary.audio_begin = audio_ranges.front().begin;
        summary.audio_end = audio_ranges.back().end;
        summary.audio_complete = summary.audio_begin < summary.audio_end && summary.pcm_bytes > 0U;
        summary.causal_output_links = output_links.size();

        for (const auto &restart : pending_restarts) {
            const auto current = voice_ranges.find(restart.occurrence);
            const auto previous = voice_ranges.find(restart.previous_occurrence);
            const auto mixes = occurrence_mixes.find(restart.occurrence);
            if (current == voice_ranges.end() || previous == voice_ranges.end() ||
                mixes == occurrence_mixes.end())
                continue;
            ++summary.restart_position_complete;
            const auto output = std::find_if(
                output_links.begin(), output_links.end(), [&mixes](const OutputLink &item) {
                    return std::ranges::find(mixes->second, item.mix) != mixes->second.end();
                });
            if (output == output_links.end())
                continue;
            ++summary.restart_mix_link_complete;
            const auto pcm = std::find_if(pcm_identity_ranges.begin(), pcm_identity_ranges.end(),
                                          [&pcm_identity_ranges, &output](const auto &item) {
                                              const auto next = &item + 1;
                                              return item.first <= output->pcm_sequence &&
                                                     (next == pcm_identity_ranges.data() +
                                                                  pcm_identity_ranges.size() ||
                                                      output->pcm_sequence < next->first);
                                          });
            if (pcm == pcm_identity_ranges.end()) {
                if (summary.first_unmatched_pcm_sequence == 0U)
                    summary.first_unmatched_pcm_sequence = output->pcm_sequence;
                continue;
            }
            ++summary.restart_output_complete;
            const auto selection = causal_parent.find(restart.request);
            if (selection == causal_parent.end())
                continue;
            ++summary.restart_request_parent_complete;
            const auto candidate = causal_parent.find(selection->second);
            if (candidate == causal_parent.end())
                continue;
            ++summary.restart_selection_parent_complete;
            const auto event = causal_parent.find(candidate->second);
            if (event == causal_parent.end())
                continue;
            ++summary.restart_candidate_parent_complete;
            const auto query_id = event->second;
            auto event_id = query_id;
            if (const auto source = causal_parent.find(query_id); source != causal_parent.end())
                event_id = source->second;
            ++summary.restart_chain_complete;
            summary.restart_candidates.push_back(
                {restart.decision, event_id, query_id, candidate->second, selection->second,
                 restart.request, output->mix, output->output, restart.reason, restart.occurrence,
                 restart.previous_occurrence, previous->second.last_source,
                 current->second.first_source, current->second.first_output,
                 current->second.last_output, output->pcm_sequence, pcm->second.begin,
                 pcm->second.end});
        }

        summary.query_origin = {std::string{run_id},
                                "engine-" + std::to_string(summary.trace.ingress.producer),
                                summary.trace.ingress.sequence};
        summary.query_mix = {std::string{run_id},
                             "engine-" + std::to_string(summary.trace.mix_span.producer),
                             summary.trace.mix_span.sequence};
        const auto mix =
            std::find_if(mix_ranges.begin(), mix_ranges.end(), [&summary](const MixRange &range) {
                return range.id == summary.query_mix;
            });
        if (mix != mix_ranges.end()) {
            const auto output = std::find_if(
                audio_ranges.begin(), audio_ranges.end(), [mix](const SampleFrameRange &range) {
                    return range.sample_rate == mix->sample_rate && range.begin < mix->end &&
                           mix->begin < range.end;
                });
            if (output != audio_ranges.end()) {
                summary.query_relation = "observed_sample_range_overlap";
                summary.query_audio_begin = (std::max)(output->begin, mix->begin);
                summary.query_audio_end = (std::min)(output->end, mix->end);
                summary.query_route_complete = summary.query_audio_begin < summary.query_audio_end;
            }
        }
        summary.complete = summary.typed_payload_complete && summary.stage_relations_complete &&
                           summary.audio_complete && summary.query_route_complete;
        return summary;
    } catch (...) {
        return CampaignAuditError::evidence_unavailable;
    }
}

std::string format_campaign_audit(const CampaignAuditSummary &summary) {
    std::ostringstream output;
    output << "audio_qa_audit: run_id=" << summary.run_id
           << " complete=" << (summary.complete ? "true" : "false")
           << " fragments=" << summary.fact_fragments << " facts=" << summary.facts
           << " typed_fields=" << summary.typed_fields
           << " invalid_typed_payloads=" << summary.invalid_typed_payloads
           << " first_invalid_typed_payload_kind="
           << (summary.first_invalid_typed_payload_kind.empty()
                   ? "none"
                   : summary.first_invalid_typed_payload_kind)
           << " first_invalid_typed_payload_id="
           << (summary.first_invalid_typed_payload_id.producer_id.empty()
                   ? "none"
                   : summary.first_invalid_typed_payload_id.producer_id + "/" +
                         std::to_string(summary.first_invalid_typed_payload_id.producer_sequence))
           << " first_invalid_typed_payload_detail="
           << (summary.first_invalid_typed_payload_detail.empty()
                   ? "none"
                   : summary.first_invalid_typed_payload_detail)
           << " assignments=" << summary.loaded_assignments
           << " selected_assignments=" << summary.selected_assignments
           << " pending_assignments=" << summary.pending_assignments << " pending_assignment_ids=[";
    for (std::size_t index{}; index < summary.pending_assignment_ids.size(); ++index) {
        if (index != 0U)
            output << ',';
        output << summary.pending_assignment_ids[index];
    }
    output << ']' << " detector_inputs=" << summary.detector_inputs
           << " detector_batches=" << summary.detector_batches
           << " candidates=" << summary.candidates << " selections=" << summary.selections
           << " requests=" << summary.playback_requests
           << " decisions=" << summary.playback_decisions << " effects=" << summary.playback_effects
           << " mix_spans=" << summary.mix_spans << " position_spans=" << summary.position_spans
           << " loop_crossings=" << summary.loop_crossings << " voice_ends=" << summary.voice_ends
           << " restart_candidates=" << summary.restart_candidates.size()
           << " main_output_spans=" << summary.main_output_spans
           << " causal_output_links=" << summary.causal_output_links
           << " restart_position_complete=" << summary.restart_position_complete
           << " restart_mix_link_complete=" << summary.restart_mix_link_complete
           << " restart_output_complete=" << summary.restart_output_complete
           << " restart_chain_complete=" << summary.restart_chain_complete
           << " restart_request_parent_complete=" << summary.restart_request_parent_complete
           << " restart_selection_parent_complete=" << summary.restart_selection_parent_complete
           << " restart_candidate_parent_complete=" << summary.restart_candidate_parent_complete
           << " first_unmatched_pcm_sequence=" << summary.first_unmatched_pcm_sequence
           << " pcm_identity_range=[" << summary.first_pcm_identity << ','
           << summary.last_pcm_identity << ']' << " pcm_blocks=" << summary.pcm_blocks
           << " pcm_bytes=" << summary.pcm_bytes << " audio=[" << summary.audio_begin << ','
           << summary.audio_end << ')'
           << " typed_payload_complete=" << (summary.typed_payload_complete ? "true" : "false")
           << " stage_relations_complete=" << (summary.stage_relations_complete ? "true" : "false")
           << " audio_complete=" << (summary.audio_complete ? "true" : "false")
           << " query_route_complete=" << (summary.query_route_complete ? "true" : "false");
    if (summary.query_route_complete)
        output << "\nroute origin=" << summary.query_origin.run_id << '/'
               << summary.query_origin.producer_id << '/' << summary.query_origin.producer_sequence
               << " mix=" << summary.query_mix.run_id << '/' << summary.query_mix.producer_id << '/'
               << summary.query_mix.producer_sequence << " audio=[" << summary.query_audio_begin
               << ',' << summary.query_audio_end << ") relation=" << summary.query_relation;
    for (const auto &restart : summary.restart_candidates) {
        output << "\nrestart decision=" << restart.decision.run_id << '/'
               << restart.decision.producer_id << '/' << restart.decision.producer_sequence
               << " reason=" << restart.reason
               << " previous_occurrence=" << restart.previous_occurrence
               << " occurrence=" << restart.occurrence << " source=" << restart.previous_source_end
               << "->" << restart.current_source_begin << " output=[" << restart.output_begin << ','
               << restart.output_end << ") chain=" << restart.event.producer_id << '/'
               << restart.event.producer_sequence;
        if (restart.query != restart.event)
            output << "->" << restart.query.producer_id << '/' << restart.query.producer_sequence;
        output << "->" << restart.candidate.producer_id << '/'
               << restart.candidate.producer_sequence << "->" << restart.selection.producer_id
               << '/' << restart.selection.producer_sequence << "->" << restart.request.producer_id
               << '/' << restart.request.producer_sequence << "->" << restart.decision.producer_id
               << '/' << restart.decision.producer_sequence << "->" << restart.mix.producer_id
               << '/' << restart.mix.producer_sequence << "->" << restart.output_span.producer_id
               << '/' << restart.output_span.producer_sequence << "->engine-7/"
               << restart.pcm_sequence << " pcm=[" << restart.pcm_begin << ',' << restart.pcm_end
               << ')';
    }
    return output.str();
}

std::string_view campaign_audit_error_code(const CampaignAuditError error) noexcept {
    switch (error) {
    case CampaignAuditError::invalid_input:
        return "invalid_input";
    case CampaignAuditError::evidence_unavailable:
        return "evidence_unavailable";
    case CampaignAuditError::fact_integrity_failed:
        return "fact_integrity_failed";
    case CampaignAuditError::audio_integrity_failed:
        return "audio_integrity_failed";
    }
    return "unknown";
}

} // namespace ayther::audio_qa
