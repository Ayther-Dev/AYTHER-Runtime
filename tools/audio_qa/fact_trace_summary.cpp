#include "fact_trace_summary.h"

#include "model_limits.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ayther::audio_qa {
namespace {

constexpr std::uint32_t unresolved_token = (std::numeric_limits<std::uint32_t>::max)();
constexpr std::uint32_t missing_token = unresolved_token - 1U;
constexpr std::size_t producer_slots = 64U;

struct CompactId {
    std::uint32_t producer{};
    std::uint64_t sequence{};
    bool operator==(const CompactId &) const = default;
};

struct CompactIdHash {
    std::size_t operator()(const CompactId id) const noexcept {
        return static_cast<std::size_t>(id.producer) * 0x9e3779b1U ^
               static_cast<std::size_t>(id.sequence ^ (id.sequence >> 32U));
    }
};

enum class TraceKind : std::uint8_t {
    other,
    ingress,
    candidate,
    selection,
    request,
    decision,
    effect,
    mix,
    output_span,
};

enum class TraceStage : std::uint8_t {
    none,
    ingress,
    candidate,
    selection,
    request,
    decision,
    effect,
    mix,
};

TraceKind trace_kind(const std::string_view kind) noexcept {
    if (kind == "detector_input" || kind == "detector_input_batch")
        return TraceKind::ingress;
    if (kind == "assignment_candidate")
        return TraceKind::candidate;
    if (kind == "assignment_selection")
        return TraceKind::selection;
    if (kind == "hd_playback_request")
        return TraceKind::request;
    if (kind == "hd_playback_decision")
        return TraceKind::decision;
    if (kind == "hd_playback_effect")
        return TraceKind::effect;
    if (kind == "hd_mix_participant")
        return TraceKind::mix;
    if (kind == "main_mix_output_span" || kind == "auxiliary_output_span")
        return TraceKind::output_span;
    return TraceKind::other;
}

std::optional<std::uint32_t> producer_number(const std::string_view value) noexcept {
    constexpr std::string_view prefix{"engine-"};
    if (!value.starts_with(prefix))
        return std::nullopt;
    const auto digits = value.substr(prefix.size());
    std::uint32_t result{};
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || result == 0U ||
        result > producer_slots)
        return std::nullopt;
    return result;
}

std::uint64_t occurrence(const Fact &fact) noexcept {
    if (!fact.occurrence_id.value)
        return 0U;
    const auto &text = *fact.occurrence_id.value;
    std::uint64_t result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() ? result : 0U;
}

} // namespace

struct ReplayFactTraceAccumulator::Impl {
    struct Chain {
        TraceStage stage{TraceStage::none};
        CompactId id;
        std::uint32_t parent{};
        std::uint32_t request_token{};
        std::uint64_t occurrence{};
    };

    struct PendingFact {
        CompactId id;
        TraceKind kind{TraceKind::other};
        std::uint64_t occurrence{};
        std::vector<CompactId> causes;
    };

    explicit Impl(std::string value) : run_id(std::move(value)) { chains.push_back({}); }

    [[nodiscard]] std::uint32_t token(const CompactId id) const noexcept {
        if (id.producer == 0U || id.producer > tokens.size() || id.sequence == 0U)
            return unresolved_token;
        const auto &producer = tokens[id.producer - 1U];
        if (id.sequence > producer.size() ||
            producer[static_cast<std::size_t>(id.sequence - 1U)] == missing_token)
            return unresolved_token;
        return producer[static_cast<std::size_t>(id.sequence - 1U)];
    }

    [[nodiscard]] std::uint32_t request_ancestor(std::uint32_t token_value) const noexcept {
        while (token_value != 0U && token_value < chains.size()) {
            const auto &chain = chains[token_value];
            if (chain.stage == TraceStage::request)
                return token_value;
            token_value = chain.parent;
        }
        return 0U;
    }

    [[nodiscard]] bool occurrence_matches(const std::uint32_t token_value,
                                          const std::uint64_t value) const noexcept {
        const auto request = request_ancestor(token_value);
        return request != 0U && value != 0U && chains[request].occurrence == value;
    }

    [[nodiscard]] std::uint32_t add_chain(const TraceStage stage, const CompactId id,
                                          const std::uint32_t parent, const std::uint64_t value) {
        if (chains.size() >= missing_token)
            throw std::bad_alloc{};
        const auto index = static_cast<std::uint32_t>(chains.size());
        const auto request = stage == TraceStage::request ? index : request_ancestor(parent);
        chains.push_back({stage, id, parent, request, value});
        return index;
    }

    [[nodiscard]] bool all_causes_resolved(const PendingFact &fact) const noexcept {
        for (const auto cause : fact.causes)
            if (token(cause) == unresolved_token)
                return false;
        return true;
    }

    [[nodiscard]] std::uint32_t best_cause(const PendingFact &fact) const noexcept {
        std::uint32_t best{};
        for (const auto cause : fact.causes) {
            const auto candidate = token(cause);
            if (candidate != 0U && candidate != unresolved_token &&
                chains[candidate].stage > chains[best].stage)
                best = candidate;
        }
        return best;
    }

    void remember_complete(const std::uint32_t request) noexcept {
        if (complete.causally_connected)
            return;
        const auto effect = effects.find(request);
        const auto mix = mixes.find(request);
        if (effect == effects.end() || mix == mixes.end())
            return;
        auto token_value = effect->second;
        while (token_value != 0U) {
            const auto &chain = chains[token_value];
            const ReplayTraceFactId id{chain.id.producer, chain.id.sequence};
            switch (chain.stage) {
            case TraceStage::ingress:
                complete.ingress = id;
                break;
            case TraceStage::candidate:
                complete.candidate = id;
                break;
            case TraceStage::selection:
                complete.selection = id;
                break;
            case TraceStage::request:
                complete.playback_request = id;
                complete.occurrence = chain.occurrence;
                break;
            case TraceStage::decision:
                complete.playback_decision = id;
                break;
            case TraceStage::effect:
                complete.playback_effect = id;
                break;
            case TraceStage::none:
            case TraceStage::mix:
                break;
            }
            token_value = chain.parent;
        }
        const auto &mix_chain = chains[mix->second];
        complete.mix_span = {mix_chain.id.producer, mix_chain.id.sequence};
        complete.causally_connected =
            complete.occurrence != 0U && complete.ingress.producer != 0U &&
            complete.candidate.producer != 0U && complete.selection.producer != 0U &&
            complete.playback_request.producer != 0U && complete.playback_decision.producer != 0U &&
            complete.playback_effect.producer != 0U && complete.mix_span.producer != 0U;
    }

    [[nodiscard]] std::uint32_t resolve(const PendingFact &fact) {
        const auto best = best_cause(fact);
        const auto best_stage = chains[best].stage;
        switch (fact.kind) {
        case TraceKind::ingress:
            return add_chain(TraceStage::ingress, fact.id, 0U, 0U);
        case TraceKind::candidate:
            if (best_stage >= TraceStage::ingress)
                return add_chain(TraceStage::candidate, fact.id, best, 0U);
            break;
        case TraceKind::selection:
            if (best_stage >= TraceStage::candidate)
                return add_chain(TraceStage::selection, fact.id, best, 0U);
            break;
        case TraceKind::request:
            if (best_stage >= TraceStage::selection && fact.occurrence != 0U)
                return add_chain(TraceStage::request, fact.id, best, fact.occurrence);
            break;
        case TraceKind::decision:
            if (best_stage >= TraceStage::request && occurrence_matches(best, fact.occurrence))
                return add_chain(TraceStage::decision, fact.id, best, fact.occurrence);
            break;
        case TraceKind::effect:
            if (best_stage >= TraceStage::decision && occurrence_matches(best, fact.occurrence))
                return add_chain(TraceStage::effect, fact.id, best, fact.occurrence);
            break;
        case TraceKind::mix:
            if (best_stage >= TraceStage::request && occurrence_matches(best, fact.occurrence))
                return add_chain(TraceStage::mix, fact.id, best, fact.occurrence);
            break;
        case TraceKind::output_span:
        case TraceKind::other:
            break;
        }
        return best;
    }

    void publish(const PendingFact &fact, const std::uint32_t value,
                 std::deque<PendingFact> &ready) {
        tokens[fact.id.producer - 1U][static_cast<std::size_t>(fact.id.sequence - 1U)] = value;
        if (value != 0U) {
            const auto request = chains[value].request_token;
            if (chains[value].stage == TraceStage::effect && request != 0U)
                effects.try_emplace(request, value);
            if (chains[value].stage == TraceStage::mix && request != 0U)
                mixes.try_emplace(request, value);
            if (request != 0U)
                remember_complete(request);
        }
        const auto found = waiting.find(fact.id);
        if (found == waiting.end())
            return;
        for (const auto dependent : found->second) {
            const auto pending = unresolved.find(dependent);
            if (pending != unresolved.end()) {
                ready.push_back(std::move(pending->second));
                unresolved.erase(pending);
            }
        }
        waiting.erase(found);
    }

    void defer(PendingFact fact) {
        const auto id = fact.id;
        const auto inserted = unresolved.emplace(id, std::move(fact));
        if (!inserted.second) {
            valid = false;
            return;
        }
        for (const auto cause : inserted.first->second.causes)
            if (token(cause) == unresolved_token)
                waiting[cause].push_back(id);
    }

    std::string run_id;
    std::array<std::vector<std::uint32_t>, producer_slots> tokens;
    std::vector<Chain> chains;
    std::unordered_map<CompactId, PendingFact, CompactIdHash> unresolved;
    std::unordered_map<CompactId, std::vector<CompactId>, CompactIdHash> waiting;
    std::unordered_map<std::uint32_t, std::uint32_t> effects;
    std::unordered_map<std::uint32_t, std::uint32_t> mixes;
    ReplayTraceSummary complete;
    std::uint64_t fact_count{};
    bool valid{true};
};

ReplayFactTraceAccumulator::ReplayFactTraceAccumulator(std::string run_id)
    : impl_(std::make_unique<Impl>(std::move(run_id))) {
    if (impl_->run_id.empty() || impl_->run_id.size() > max_identity_bytes)
        impl_->valid = false;
}

ReplayFactTraceAccumulator::~ReplayFactTraceAccumulator() = default;
ReplayFactTraceAccumulator::ReplayFactTraceAccumulator(ReplayFactTraceAccumulator &&) noexcept =
    default;
ReplayFactTraceAccumulator &
ReplayFactTraceAccumulator::operator=(ReplayFactTraceAccumulator &&) noexcept = default;

bool ReplayFactTraceAccumulator::consume(const Fact &fact) noexcept {
    if (!impl_ || !impl_->valid)
        return false;
    try {
        const auto producer = producer_number(fact.id.producer_id);
        if (!producer || fact.id.run_id != impl_->run_id || fact.id.producer_sequence == 0U)
            return impl_->valid = false;
        if (fact.id.producer_sequence > max_replay_trace_facts)
            return impl_->valid = false;
        auto &tokens = impl_->tokens[*producer - 1U];
        if (tokens.size() < fact.id.producer_sequence)
            tokens.resize(static_cast<std::size_t>(fact.id.producer_sequence), missing_token);
        auto &token = tokens[static_cast<std::size_t>(fact.id.producer_sequence - 1U)];
        if (token != missing_token)
            return impl_->valid = false;
        token = unresolved_token;

        Impl::PendingFact compact{
            {*producer, fact.id.producer_sequence}, trace_kind(fact.kind), occurrence(fact), {}};
        compact.causes.reserve(fact.cause_ids.size());
        for (const auto &cause : fact.cause_ids) {
            const auto *id = std::get_if<FactId>(&cause);
            if (id == nullptr)
                continue;
            const auto cause_producer = producer_number(id->producer_id);
            if (!cause_producer || id->run_id != impl_->run_id || id->producer_sequence == 0U)
                return impl_->valid = false;
            constexpr std::uint32_t main_output_pcm_producer = 7U;
            if (compact.kind == TraceKind::output_span &&
                *cause_producer == main_output_pcm_producer)
                continue;
            compact.causes.push_back({*cause_producer, id->producer_sequence});
        }
        ++impl_->fact_count;
        std::deque<Impl::PendingFact> ready;
        ready.push_back(std::move(compact));
        while (!ready.empty() && impl_->valid) {
            auto current = std::move(ready.front());
            ready.pop_front();
            if (!impl_->all_causes_resolved(current)) {
                impl_->defer(std::move(current));
                continue;
            }
            const auto value = impl_->resolve(current);
            impl_->publish(current, value, ready);
        }
        return impl_->valid;
    } catch (...) {
        impl_->valid = false;
        return false;
    }
}

ReplayTraceSummary
ReplayFactTraceAccumulator::summarize(const bool transport_loss_free) const noexcept {
    ReplayTraceSummary result;
    if (!impl_)
        return result;
    result = impl_->complete;
    result.observed_fact_count = impl_->fact_count;
    bool sequences_complete = true;
    for (const auto &producer : impl_->tokens)
        for (const auto token : producer)
            sequences_complete = sequences_complete && token != missing_token;
    result.loss_free =
        transport_loss_free && impl_->valid && sequences_complete && impl_->unresolved.empty();
    if (!result.loss_free)
        result.causally_connected = false;
    return result;
}

ReplayTraceSummary summarize_replay_facts(const std::span<const Fact> facts,
                                          const bool loss_free) noexcept {
    if (facts.empty())
        return {};
    ReplayFactTraceAccumulator accumulator{facts.front().id.run_id};
    for (const auto &fact : facts)
        if (!accumulator.consume(fact))
            break;
    return accumulator.summarize(loss_free);
}

} // namespace ayther::audio_qa
