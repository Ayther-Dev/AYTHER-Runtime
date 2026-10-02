#include "replay_trace.h"

#include "model_limits.h"
#include "pcm_message.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ayther::audio_qa {
namespace {

namespace observation = engine::audio_observation;

[[nodiscard]] std::string producer_id(const std::uint32_t producer) {
    return "engine-" + std::to_string(producer);
}

[[nodiscard]] FactId fact_id(const std::string_view run_id, const observation::FactId id) {
    return {std::string{run_id}, producer_id(id.producer), id.sequence};
}

[[nodiscard]] FactFieldUnit field_unit(const observation::Unit unit) noexcept {
    switch (unit) {
    case observation::Unit::none:
        return FactFieldUnit::none;
    case observation::Unit::emulation_frame:
        return FactFieldUnit::emulation_frame;
    case observation::Unit::sample_frame:
        return FactFieldUnit::sample_frame;
    case observation::Unit::bytes:
        return FactFieldUnit::bytes;
    case observation::Unit::count:
        return FactFieldUnit::count;
    case observation::Unit::linear_gain:
        return FactFieldUnit::linear_gain;
    case observation::Unit::frames_per_second:
        return FactFieldUnit::frames_per_second;
    case observation::Unit::nanoseconds:
        return FactFieldUnit::nanoseconds;
    }
    return FactFieldUnit::none;
}

[[nodiscard]] FactFieldValue field_value(const std::string_view run_id,
                                         const observation::Value &value) {
    return std::visit(
        [run_id](const auto &item) -> FactFieldValue {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::string_view>)
                return std::string{item};
            else if constexpr (std::is_same_v<T, observation::FactId>)
                return fact_id(run_id, item);
            else if constexpr (std::is_same_v<T, observation::OccurrenceId>)
                return item.value;
            else
                return item;
        },
        value);
}

template <class T> [[nodiscard]] Field<T> unavailable(std::string reason) {
    return {Availability::not_applicable, std::nullopt, std::move(reason)};
}

template <class T> [[nodiscard]] Field<T> compact_unavailable() {
    return {Availability::not_applicable, std::nullopt, "compact_default"};
}

[[nodiscard]] std::optional<std::string> text_field(const observation::FactView &fact,
                                                    const std::string_view name) {
    for (const auto &field : fact.fields) {
        if (field.name == name && field.availability == observation::Availability::known) {
            if (const auto *value = std::get_if<std::string_view>(&field.value))
                return std::string{*value};
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t> number_field(const observation::FactView &fact,
                                                        const std::string_view name) {
    for (const auto &field : fact.fields) {
        if (field.name == name && field.availability == observation::Availability::known) {
            if (const auto *value = std::get_if<std::uint64_t>(&field.value))
                return *value;
            if (const auto *occurrence = std::get_if<observation::OccurrenceId>(&field.value))
                return occurrence->value;
        }
    }
    return std::nullopt;
}

[[nodiscard]] Fact durable_fact(const std::string_view run_id,
                                const observation::FactView &source) {
    Fact result;
    result.id = fact_id(run_id, source.id);
    result.kind = source.kind;
    switch (source.frame.availability) {
    case observation::Availability::known:
        result.frame_index = {Availability::known, source.frame.emulation_frame, {}};
        break;
    case observation::Availability::not_applicable:
        result.frame_index = {Availability::not_applicable, std::nullopt,
                              std::string{source.frame.unavailable_reason}};
        break;
    case observation::Availability::unknown:
        result.frame_index = {Availability::unknown, std::nullopt,
                              std::string{source.frame.unavailable_reason}};
        break;
    }
    result.cause_ids.reserve(source.causes.size());
    for (const auto &cause : source.causes) {
        if (const auto *id = std::get_if<observation::FactId>(&cause))
            result.cause_ids.emplace_back(fact_id(run_id, *id));
        else
            result.cause_ids.emplace_back(PreexistingContext{
                std::string{std::get<observation::PreexistingContext>(cause).state_id}});
    }
    const auto local_id =
        producer_id(source.id.producer) + ":" + std::to_string(source.id.sequence);
    result.decision_id = source.kind == "hd_playback_decision"
                             ? Field<std::string>{Availability::known, local_id, {}}
                             : compact_unavailable<std::string>();
    auto assignment = number_field(source, "assignment_signature");
    if (!assignment)
        assignment = number_field(source, "selected_signature");
    result.assignment_id =
        assignment ? Field<std::string>{Availability::known, std::to_string(*assignment), {}}
                   : compact_unavailable<std::string>();
    const auto occurrence = number_field(source, "occurrence");
    result.occurrence_id =
        occurrence ? Field<std::string>{Availability::known, std::to_string(*occurrence), {}}
                   : compact_unavailable<std::string>();
    const auto reason = text_field(source, "reason");
    result.reason_code = reason ? Field<std::string>{Availability::known, *reason, {}}
                                : compact_unavailable<std::string>();
    if (source.state_orders.empty()) {
        result.shared_state_order = compact_unavailable<std::vector<SharedStateOrder>>();
    } else {
        std::vector<SharedStateOrder> orders;
        orders.reserve(source.state_orders.size());
        for (const auto &order : source.state_orders)
            orders.push_back({std::string{order.state_id}, order.sequence});
        result.shared_state_order = {Availability::known, std::move(orders), {}};
    }
    result.fields.reserve(source.fields.size());
    for (const auto &field : source.fields) {
        Availability availability{Availability::unknown};
        switch (field.availability) {
        case observation::Availability::known:
            availability = Availability::known;
            break;
        case observation::Availability::not_applicable:
            availability = Availability::not_applicable;
            break;
        case observation::Availability::unknown:
            availability = Availability::unknown;
            break;
        }
        result.fields.push_back({std::string{field.name}, availability, field_unit(field.unit),
                                 field_value(run_id, field.value),
                                 std::string{field.unavailable_reason}});
    }
    return result;
}

[[nodiscard]] std::optional<PcmFormat> pcm_format(const observation::PcmFormat format) noexcept {
    switch (format) {
    case observation::PcmFormat::s16_le:
        return PcmFormat::s16le;
    case observation::PcmFormat::s24_le:
        return PcmFormat::s24le;
    case observation::PcmFormat::s32_le:
        return PcmFormat::s32le;
    case observation::PcmFormat::f32_le:
        return PcmFormat::f32le;
    }
    return std::nullopt;
}

} // namespace

ReplayTraceCollector::ReplayTraceCollector(std::string run_id) : run_id_(std::move(run_id)) {
    valid_ = !run_id_.empty() && run_id_.size() <= max_identity_bytes;
    facts_.reserve(256U);
    durable_facts_.reserve(256U);
    audio_chunks_.reserve(16U);
}

std::optional<Fact>
copy_replay_trace_fact(const std::string_view run_id,
                       const engine::audio_observation::FactView &fact) noexcept {
    try {
        if (run_id.empty() || run_id.size() > max_identity_bytes || fact.id.producer == 0U ||
            fact.id.sequence == 0U || fact.kind.empty())
            return std::nullopt;
        auto copied = durable_fact(run_id, fact);
        if (!well_formed(copied))
            return std::nullopt;
        return copied;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<AudioChunk>
copy_replay_trace_pcm(const std::string_view run_id,
                      const engine::audio_observation::PcmView &pcm) noexcept {
    try {
        if (run_id.empty() || run_id.size() > max_identity_bytes || pcm.id.producer == 0U ||
            pcm.id.sequence == 0U)
            return std::nullopt;
        const auto format = pcm_format(pcm.format);
        if (!format || pcm.channels > (std::numeric_limits<std::uint8_t>::max)())
            return std::nullopt;
        AudioChunk chunk;
        chunk.run_id = run_id;
        chunk.capture_point = pcm.capture_point;
        chunk.producer_sequence = pcm.id.sequence;
        chunk.format = {*format, pcm.range.sample_rate, static_cast<std::uint8_t>(pcm.channels)};
        chunk.range = {std::string{pcm.range.timeline}, pcm.range.sample_rate, pcm.range.begin,
                       pcm.range.end};
        chunk.bytes.assign(pcm.bytes.begin(), pcm.bytes.end());
        chunk.sha256 = {Availability::known, pcm_sha256(chunk.bytes), {}};
        chunk.durability = Durability::pending;
        chunk.checkpoint_id = unavailable<std::string>("not_published_by_runtime");
        chunk.cause_ids.reserve(pcm.causes.size());
        for (const auto &cause : pcm.causes)
            if (const auto *id = std::get_if<observation::FactId>(&cause))
                chunk.cause_ids.push_back(fact_id(run_id, *id));
        if (!well_formed(chunk))
            return std::nullopt;
        return chunk;
    } catch (...) {
        return std::nullopt;
    }
}

void ReplayTraceCollector::consume(const std::string_view run_id,
                                   const engine::audio_observation::FactView &fact) noexcept {
    namespace observation = engine::audio_observation;
    if (!valid_ || run_id != run_id_ || fact.id.producer == 0U || fact.id.sequence == 0U ||
        fact.kind.empty() || !replay_trace_fact_count_within_limit(facts_.size() + 1U)) {
        valid_ = false;
        return;
    }
    try {
        Node node;
        node.id = {fact.id.producer, fact.id.sequence};
        node.kind = fact.kind;
        node.causes.reserve(fact.causes.size());
        for (const auto &cause : fact.causes) {
            if (const auto *id = std::get_if<observation::FactId>(&cause))
                node.causes.push_back({id->producer, id->sequence});
        }
        for (const auto &field : fact.fields) {
            if (field.name == "occurrence" &&
                field.availability == observation::Availability::known) {
                if (const auto *occurrence = std::get_if<observation::OccurrenceId>(&field.value))
                    node.occurrence = occurrence->value;
            }
        }
        facts_.push_back(std::move(node));
        auto durable = copy_replay_trace_fact(run_id, fact);
        if (!durable) {
            valid_ = false;
            return;
        }
        durable_facts_.push_back(std::move(*durable));
    } catch (...) {
        valid_ = false;
    }
}

void ReplayTraceCollector::consume(const std::string_view run_id,
                                   const engine::audio_observation::PcmView &pcm) noexcept {
    if (!valid_ || run_id != run_id_ || pcm.id.producer == 0U || pcm.id.sequence == 0U ||
        !replay_trace_fact_count_within_limit(audio_chunks_.size() + 1U)) {
        valid_ = false;
        return;
    }
    try {
        auto chunk = copy_replay_trace_pcm(run_id, pcm);
        if (!chunk) {
            valid_ = false;
            return;
        }
        audio_chunks_.push_back(std::move(*chunk));
    } catch (...) {
        valid_ = false;
    }
}

ReplayTraceSummary ReplayTraceCollector::summarize(const bool bridge_loss_free) const {
    ReplayTraceSummary result;
    result.observed_fact_count = facts_.size();
    result.loss_free = valid_ && bridge_loss_free;
    if (!result.loss_free)
        return result;

    struct IdHash {
        [[nodiscard]] std::size_t operator()(const ReplayTraceFactId id) const noexcept {
            const auto first = static_cast<std::size_t>(id.producer);
            const auto second = static_cast<std::size_t>(id.sequence ^ (id.sequence >> 32U));
            return first * 0x9e3779b1U ^ second;
        }
    };
    std::unordered_map<ReplayTraceFactId, const Node *, IdHash> by_id;
    by_id.reserve(facts_.size());
    for (const auto &node : facts_)
        by_id.emplace(node.id, &node);

    const auto ancestor_of_kind = [&](const Node &descendant,
                                      const std::span<const std::string_view> kinds) {
        std::vector<ReplayTraceFactId> pending{descendant.causes.begin(), descendant.causes.end()};
        std::unordered_set<ReplayTraceFactId, IdHash> visited;
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            if (!visited.emplace(current).second)
                continue;
            const auto found = by_id.find(current);
            if (found == by_id.end())
                continue;
            const auto *node = found->second;
            if (std::find(kinds.begin(), kinds.end(), node->kind) != kinds.end())
                return node;
            pending.insert(pending.end(), node->causes.begin(), node->causes.end());
        }
        return static_cast<const Node *>(nullptr);
    };
    const auto nodes = [&](const std::string_view kind) {
        std::vector<const Node *> matches;
        for (const auto &node : facts_)
            if (node.kind == kind)
                matches.push_back(&node);
        return matches;
    };

    const auto remember_first = [](const std::vector<const Node *> &matches,
                                   ReplayTraceFactId &id) {
        if (!matches.empty())
            id = matches.front()->id;
    };
    auto ingress_nodes = nodes("detector_input_batch");
    const auto individual_ingress_nodes = nodes("detector_input");
    ingress_nodes.insert(ingress_nodes.end(), individual_ingress_nodes.begin(),
                         individual_ingress_nodes.end());
    const auto candidate_nodes = nodes("assignment_candidate");
    const auto selection_nodes = nodes("assignment_selection");
    const auto request_nodes = nodes("hd_playback_request");
    const auto decision_nodes = nodes("hd_playback_decision");
    const auto effect_nodes = nodes("hd_playback_effect");
    const auto mix_nodes = nodes("hd_mix_participant");
    remember_first(ingress_nodes, result.ingress);
    remember_first(candidate_nodes, result.candidate);
    remember_first(selection_nodes, result.selection);
    remember_first(request_nodes, result.playback_request);
    remember_first(decision_nodes, result.playback_decision);
    remember_first(effect_nodes, result.playback_effect);
    remember_first(mix_nodes, result.mix_span);
    if (!request_nodes.empty())
        result.occurrence = request_nodes.front()->occurrence;

    constexpr std::array<std::string_view, 1> request_kind{"hd_playback_request"};
    constexpr std::array<std::string_view, 1> decision_kind{"hd_playback_decision"};
    constexpr std::array<std::string_view, 1> selection_kind{"assignment_selection"};
    constexpr std::array<std::string_view, 1> candidate_kind{"assignment_candidate"};
    constexpr std::array<std::string_view, 2> ingress_kinds{"detector_input_batch",
                                                            "detector_input"};
    for (const auto *mix : mix_nodes) {
        const auto *request = ancestor_of_kind(*mix, request_kind);
        if (request == nullptr || request->occurrence == 0U ||
            mix->occurrence != request->occurrence)
            continue;
        const auto decision =
            std::find_if(decision_nodes.begin(), decision_nodes.end(), [&](const Node *node) {
                return node->occurrence == request->occurrence &&
                       ancestor_of_kind(*node, request_kind) == request;
            });
        if (decision == decision_nodes.end())
            continue;
        const auto effect =
            std::find_if(effect_nodes.begin(), effect_nodes.end(), [&](const Node *node) {
                return node->occurrence == request->occurrence &&
                       ancestor_of_kind(*node, decision_kind) == *decision;
            });
        if (effect == effect_nodes.end())
            continue;
        const auto *selection = ancestor_of_kind(*request, selection_kind);
        const auto *candidate =
            selection == nullptr ? nullptr : ancestor_of_kind(*selection, candidate_kind);
        const auto *ingress =
            candidate == nullptr ? nullptr : ancestor_of_kind(*candidate, ingress_kinds);
        if (selection == nullptr || candidate == nullptr || ingress == nullptr)
            continue;
        result.ingress = ingress->id;
        result.candidate = candidate->id;
        result.selection = selection->id;
        result.playback_request = request->id;
        result.playback_decision = (*decision)->id;
        result.playback_effect = (*effect)->id;
        result.mix_span = mix->id;
        result.occurrence = request->occurrence;
        result.causally_connected = true;
        return result;
    }
    return result;
}

bool ReplayTraceCollector::valid() const noexcept { return valid_; }

const std::vector<Fact> &ReplayTraceCollector::facts() const noexcept { return durable_facts_; }

const std::vector<AudioChunk> &ReplayTraceCollector::audio_chunks() const noexcept {
    return audio_chunks_;
}

ReplayTraceSummary summarize_engine_replay_facts(const std::span<const Fact> facts,
                                                 const bool loss_free) noexcept {
    ReplayTraceSummary result;
    result.observed_fact_count = facts.size();
    result.loss_free = loss_free;
    if (!loss_free)
        return result;
    try {
        struct IdHash {
            std::size_t operator()(const FactId &id) const noexcept {
                auto value = std::hash<std::string>{}(id.run_id);
                value ^= std::hash<std::string>{}(id.producer_id) + 0x9e3779b9U + (value << 6U) +
                         (value >> 2U);
                value ^= std::hash<std::uint64_t>{}(id.producer_sequence) + 0x9e3779b9U +
                         (value << 6U) + (value >> 2U);
                return value;
            }
        };
        const auto trace_id = [](const FactId &id) -> ReplayTraceFactId {
            constexpr std::string_view prefix{"engine-"};
            if (!id.producer_id.starts_with(prefix))
                return {};
            std::uint32_t producer{};
            const auto text = std::string_view{id.producer_id}.substr(prefix.size());
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), producer);
            return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()
                       ? ReplayTraceFactId{producer, id.producer_sequence}
                       : ReplayTraceFactId{};
        };
        const auto occurrence = [](const Fact &fact) -> std::uint64_t {
            if (!fact.occurrence_id.value)
                return 0U;
            std::uint64_t value{};
            const auto &text = *fact.occurrence_id.value;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() ? value : 0U;
        };
        std::unordered_map<FactId, const Fact *, IdHash> by_id;
        by_id.reserve(facts.size());
        for (const auto &fact : facts)
            by_id.emplace(fact.id, &fact);
        const auto ancestor = [&](const Fact &descendant, const std::string_view kind) {
            std::vector<FactId> pending;
            for (const auto &cause : descendant.cause_ids)
                if (const auto *id = std::get_if<FactId>(&cause))
                    pending.push_back(*id);
            std::unordered_set<FactId, IdHash> visited;
            while (!pending.empty()) {
                auto current = std::move(pending.back());
                pending.pop_back();
                if (!visited.emplace(current).second)
                    continue;
                const auto found = by_id.find(current);
                if (found == by_id.end())
                    continue;
                if (found->second->kind == kind)
                    return found->second;
                for (const auto &cause : found->second->cause_ids)
                    if (const auto *id = std::get_if<FactId>(&cause))
                        pending.push_back(*id);
            }
            return static_cast<const Fact *>(nullptr);
        };
        std::vector<const Fact *> requests;
        std::vector<const Fact *> decisions;
        std::vector<const Fact *> effects;
        std::vector<const Fact *> mixes;
        for (const auto &fact : facts) {
            if (fact.kind == "hd_playback_request")
                requests.push_back(&fact);
            else if (fact.kind == "hd_playback_decision")
                decisions.push_back(&fact);
            else if (fact.kind == "hd_playback_effect")
                effects.push_back(&fact);
            else if (fact.kind == "hd_mix_participant")
                mixes.push_back(&fact);
        }
        if (!requests.empty()) {
            result.playback_request = trace_id(requests.front()->id);
            result.occurrence = occurrence(*requests.front());
        }
        for (const auto *mix : mixes) {
            const auto *request = ancestor(*mix, "hd_playback_request");
            if (request == nullptr || occurrence(*request) == 0U ||
                occurrence(*mix) != occurrence(*request))
                continue;
            const auto decision =
                std::find_if(decisions.begin(), decisions.end(), [&](const Fact *fact) {
                    return occurrence(*fact) == occurrence(*request) &&
                           ancestor(*fact, "hd_playback_request") == request;
                });
            if (decision == decisions.end())
                continue;
            const auto effect = std::find_if(effects.begin(), effects.end(), [&](const Fact *fact) {
                return occurrence(*fact) == occurrence(*request) &&
                       ancestor(*fact, "hd_playback_decision") == *decision;
            });
            if (effect == effects.end())
                continue;
            const auto *selection = ancestor(*request, "assignment_selection");
            const auto *candidate =
                selection == nullptr ? nullptr : ancestor(*selection, "assignment_candidate");
            const Fact *ingress = nullptr;
            if (candidate != nullptr) {
                ingress = ancestor(*candidate, "detector_input_batch");
                if (ingress == nullptr)
                    ingress = ancestor(*candidate, "detector_input");
            }
            if (selection == nullptr || candidate == nullptr || ingress == nullptr)
                continue;
            result.ingress = trace_id(ingress->id);
            result.candidate = trace_id(candidate->id);
            result.selection = trace_id(selection->id);
            result.playback_request = trace_id(request->id);
            result.playback_decision = trace_id((*decision)->id);
            result.playback_effect = trace_id((*effect)->id);
            result.mix_span = trace_id(mix->id);
            result.occurrence = occurrence(*request);
            result.causally_connected = true;
            return result;
        }
    } catch (...) {
        result.causally_connected = false;
    }
    return result;
}

void consume_replay_trace_fact(void *const context, const std::string_view run_id,
                               const engine::audio_observation::FactView &fact) noexcept {
    if (context != nullptr)
        static_cast<ReplayTraceCollector *>(context)->consume(run_id, fact);
}

void consume_replay_trace_pcm(void *const context, const std::string_view run_id,
                              const engine::audio_observation::PcmView &pcm) noexcept {
    if (context != nullptr)
        static_cast<ReplayTraceCollector *>(context)->consume(run_id, pcm);
}

} // namespace ayther::audio_qa
