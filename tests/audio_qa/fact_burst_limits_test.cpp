#include "fact_batch.h"
#include "observation_bridge.h"
#include "protocol_header.h"

#include <ayther/engine/audio_observer.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace obs = ayther::engine::audio_observation;
namespace {

constexpr std::uint64_t burst_fact_count = 100'000U;
constexpr std::uint64_t replay_detector_burst_count = 4096U;
constexpr std::uint64_t replay_detector_output_burst_count = 1024U;
// The delivered Release replay can outrun the persistence worker by more than
// 4096 selection facts while closing a long, three-cycle take. Preserve 50%
// headroom over that former limit without relying on debug-build pacing.
constexpr std::uint64_t replay_selection_burst_count = 6144U;
constexpr std::uint64_t replay_mixer_burst_count = 384U;
constexpr std::size_t framed_fact_bytes = 256U;

void require(const bool condition, const char *const message) {
    if (!condition)
        throw std::runtime_error(message);
}

qa::Fact compact_fact(const std::uint64_t sequence, std::string kind = "k") {
    qa::Fact fact;
    fact.id = {"r", "p", sequence};
    fact.kind = std::move(kind);
    fact.frame_index = {qa::Availability::known, sequence, {}};
    fact.decision_id = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    fact.assignment_id = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    fact.occurrence_id = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    fact.reason_code = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    fact.shared_state_order = {qa::Availability::not_applicable, std::nullopt, "compact_default"};
    return fact;
}

qa::Fact full_fact() {
    qa::Fact fact;
    fact.id = {"run-limit", "producer", 1U};
    fact.kind = "limit";
    fact.frame_index = {qa::Availability::known, 1U, {}};
    fact.decision_id = {qa::Availability::not_applicable, std::nullopt, "not_a_decision"};
    fact.assignment_id = {qa::Availability::not_applicable, std::nullopt, "assignment_unavailable"};
    fact.occurrence_id = {qa::Availability::not_applicable, std::nullopt, "occurrence_unavailable"};
    fact.reason_code = {qa::Availability::not_applicable, std::nullopt, "reason_unavailable"};
    fact.shared_state_order = {qa::Availability::not_applicable, std::nullopt, "independent"};
    return fact;
}

std::size_t encoded_record_bytes(const qa::Fact &fact) {
    const auto encoded = qa::encode_fact_batch(std::vector{fact}, 1U);
    const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
    require(message != nullptr, "fact_could_not_be_measured");
    return message->size() - qa::protocol_header_bytes - sizeof(std::uint32_t);
}

qa::Fact fact_with_record_bytes(const std::uint64_t sequence, const std::size_t target) {
    auto fact = compact_fact(sequence);
    const auto base = encoded_record_bytes(fact);
    require(base <= target && target - base < qa::max_identity_bytes,
            "compact_target_out_of_range");
    fact.kind.append(target - base, 'k');
    require(encoded_record_bytes(fact) == target, "compact_target_size_mismatch");
    return fact;
}

std::string full_context(const std::size_t index) {
    auto value = std::string{"cause-"} + std::to_string(index) + "-";
    value.append(qa::max_identity_bytes - value.size(), 'x');
    return value;
}

qa::Fact maximum_fact() {
    constexpr std::size_t target = qa::max_encoded_fact_bytes;
    for (std::size_t full_count = 0U; full_count < qa::max_fact_causes; ++full_count) {
        auto candidate = full_fact();
        candidate.cause_ids.reserve(full_count + 1U);
        for (std::size_t index = 0; index < full_count; ++index)
            candidate.cause_ids.emplace_back(qa::PreexistingContext{full_context(index)});
        const auto tail_prefix = std::string{"tail-"} + std::to_string(full_count) + "-";
        candidate.cause_ids.emplace_back(qa::PreexistingContext{tail_prefix});
        const auto encoded = qa::encode_fact_batch(std::vector{candidate}, 1U);
        const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
        if (message == nullptr)
            continue;
        const auto size = message->size() - qa::protocol_header_bytes - sizeof(std::uint32_t);
        if (size <= target && target - size <= qa::max_identity_bytes - tail_prefix.size()) {
            auto &tail = std::get<qa::PreexistingContext>(candidate.cause_ids.back());
            tail.context_id.append(target - size, 't');
            require(encoded_record_bytes(candidate) == target, "maximum_fact_size_mismatch");
            return candidate;
        }
    }
    throw std::runtime_error("maximum_fact_fixture_unavailable");
}

struct Sink {
    std::uint64_t received{};
    std::uint64_t expected_sequence{1U};
    bool valid{true};

    static void receive(void *const context, const std::string_view run_id,
                        const qa::EngineFactView &fact) noexcept {
        auto &sink = *static_cast<Sink *>(context);
        sink.valid = sink.valid && run_id == "run-burst" && fact.id.producer == 1U &&
                     fact.id.sequence == sink.expected_sequence;
        ++sink.expected_sequence;
        ++sink.received;
    }
};

struct ProductionSink {
    std::uint32_t producer{};
    std::uint64_t received{};
    std::uint64_t expected_sequence{1U};
    bool valid{true};

    static void receive(void *const context, const std::string_view run_id,
                        const qa::EngineFactView &fact) noexcept {
        auto &sink = *static_cast<ProductionSink *>(context);
        sink.valid = sink.valid && run_id == "run-production-burst" &&
                     fact.id.producer == sink.producer &&
                     fact.id.sequence == sink.expected_sequence;
        ++sink.expected_sequence;
        ++sink.received;
    }
};

struct BalancedSink {
    std::array<std::uint64_t, 9> expected_sequences{1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U, 1U};
    std::array<std::uint64_t, 9> received{};
    std::uint32_t first_producer{};
    bool valid{true};

    static void receive(void *const context, const std::string_view run_id,
                        const qa::EngineFactView &fact) noexcept {
        auto &sink = *static_cast<BalancedSink *>(context);
        if (fact.id.producer == 0U || fact.id.producer > sink.received.size()) {
            sink.valid = false;
            return;
        }
        const auto index = static_cast<std::size_t>(fact.id.producer - 1U);
        if (sink.first_producer == 0U)
            sink.first_producer = fact.id.producer;
        sink.valid = sink.valid && run_id == "run-balanced" &&
                     fact.id.sequence == sink.expected_sequences[index];
        ++sink.expected_sequences[index];
        ++sink.received[index];
    }
};

struct FairSink {
    std::uint32_t previous{};
    std::size_t current_run{};
    std::size_t longest_run{};
    std::size_t first_run{};
    std::size_t received{};

    static void receive(void *const context, std::string_view,
                        const qa::EngineFactView &fact) noexcept {
        auto &sink = *static_cast<FairSink *>(context);
        if (sink.previous != 0U && fact.id.producer != sink.previous && sink.first_run == 0U)
            sink.first_run = sink.current_run;
        sink.current_run = fact.id.producer == sink.previous ? sink.current_run + 1U : 1U;
        sink.previous = fact.id.producer;
        sink.longest_run = (std::max)(sink.longest_run, sink.current_run);
        ++sink.received;
    }
};

} // namespace

int main() {
    try {
        const auto max_fact = maximum_fact();
        const auto max_encoded = qa::encode_fact_batch(std::vector{max_fact}, 1U);
        const auto *max_message = std::get_if<std::vector<std::byte>>(&max_encoded);
        require(max_message != nullptr &&
                    encoded_record_bytes(max_fact) == qa::max_encoded_fact_bytes,
                "maximum_fact_rejected");
        auto oversized_fact = max_fact;
        oversized_fact.kind.push_back('x');
        require(qa::encode_fact_batch(std::vector{oversized_fact}, 1U) ==
                    qa::EncodedFactBatch{qa::FactBatchError::fact_too_large},
                "oversized_fact_not_diagnosed");

        constexpr std::size_t full_record_count = 1023U;
        constexpr std::size_t last_record_bytes = qa::max_protocol_payload_bytes -
                                                  sizeof(std::uint32_t) -
                                                  full_record_count * framed_fact_bytes;
        static_assert(last_record_bytes < framed_fact_bytes);
        std::vector<qa::Fact> maximum_batch;
        maximum_batch.reserve(qa::max_fact_batch_records);
        for (std::size_t index = 0; index < full_record_count; ++index)
            maximum_batch.push_back(fact_with_record_bytes(index + 1U, framed_fact_bytes));
        maximum_batch.push_back(
            fact_with_record_bytes(qa::max_fact_batch_records, last_record_bytes));
        const auto batch_encoded = qa::encode_fact_batch(maximum_batch, 2U);
        const auto *batch_message = std::get_if<std::vector<std::byte>>(&batch_encoded);
        require(batch_message != nullptr &&
                    batch_message->size() ==
                        qa::protocol_header_bytes + qa::max_protocol_payload_bytes,
                "maximum_batch_rejected");
        const auto batch_decoded = qa::decode_fact_batch(*batch_message, 2U);
        require(std::holds_alternative<std::vector<qa::Fact>>(batch_decoded) &&
                    std::get<std::vector<qa::Fact>>(batch_decoded) == maximum_batch,
                "maximum_batch_round_trip_failed");
        maximum_batch.back().kind.push_back('x');
        require(qa::encode_fact_batch(maximum_batch, 2U) ==
                    qa::EncodedFactBatch{qa::FactBatchError::batch_too_large},
                "oversized_batch_not_diagnosed");
        maximum_batch.push_back(compact_fact(1025U));
        require(qa::encode_fact_batch(maximum_batch, 2U) ==
                    qa::EncodedFactBatch{qa::FactBatchError::invalid_count},
                "oversized_batch_count_not_diagnosed");

        using Bridge = qa::RuntimeObservationBridge<1U, 128U, 1U>;
        auto bridge = std::make_unique<Bridge>("run-burst");
        const auto observer = bridge->observer();
        Sink sink;
        const auto started = std::chrono::steady_clock::now();
        for (std::uint64_t sequence = 1U; sequence <= burst_fact_count; ++sequence) {
            observer.observe(obs::FactView{{1U, sequence},
                                           "burst_fact",
                                           {obs::Availability::known, sequence, {}},
                                           {},
                                           {},
                                           {}});
            require(bridge->try_consume_fact(&sink, Sink::receive), "burst_fact_not_received");
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
        const auto metrics = bridge->metrics();
        require(elapsed >= 0 && elapsed <= 1'000'000 && sink.valid &&
                    sink.received == burst_fact_count &&
                    metrics.attempted_facts == burst_fact_count &&
                    metrics.enqueued_facts == burst_fact_count &&
                    metrics.consumed_facts == burst_fact_count &&
                    metrics.current_fact_occupancy == 0U && metrics.maximum_fact_occupancy == 1U &&
                    bridge->losses() == qa::ObservationBridgeLosses{},
                "burst_transport_contract_failed");

        const auto verify_production_burst = [](const std::uint32_t producer,
                                                const std::uint64_t count, const char *const kind,
                                                const char *const failure) {
            auto production_bridge =
                std::make_unique<qa::ProductionObservationBridge>("run-production-burst");
            const auto production_observer = production_bridge->observer();
            const std::array<obs::Cause, 3> detector_causes{
                obs::FactId{1U, 1U}, obs::FactId{2U, 1U}, obs::FactId{3U, 1U}};
            const std::array<obs::Cause, 4> selection_causes{
                obs::FactId{1U, 1U}, obs::FactId{2U, 1U}, obs::FactId{3U, 1U}, obs::FactId{4U, 1U}};
            const std::string detector_field_name(24U, 'n');
            const std::string detector_field_value(32U, 'v');
            std::array<obs::FieldView, 24> detector_fields{};
            for (auto &field : detector_fields) {
                field = {detector_field_name,
                         obs::Availability::known,
                         obs::Unit::none,
                         std::string_view{detector_field_value},
                         {}};
            }
            const std::array<obs::FieldView, 11> selection_fields{
                obs::FieldView{"signature", obs::Availability::known, obs::Unit::none,
                               std::uint64_t{1U}, {}},
                obs::FieldView{"instrument", obs::Availability::known, obs::Unit::none,
                               std::uint64_t{2U}, {}},
                obs::FieldView{"pitch", obs::Availability::known, obs::Unit::none,
                               std::uint64_t{3U}, {}},
                obs::FieldView{"use", obs::Availability::known, obs::Unit::none,
                               std::string_view{"runtime_selection"}, {}},
                obs::FieldView{"source", obs::Availability::unknown, obs::Unit::none,
                               std::monostate{}, "not_observed"},
                obs::FieldView{"provenance_complete", obs::Availability::known,
                               obs::Unit::none, false, {}},
                obs::FieldView{"source_kind", obs::Availability::known, obs::Unit::none,
                               std::string_view{"detector_event"}, {}},
                obs::FieldView{"source_index", obs::Availability::known, obs::Unit::count,
                               std::uint64_t{4U}, {}},
                obs::FieldView{"event_start", obs::Availability::known,
                               obs::Unit::emulation_frame, std::uint64_t{5U}, {}},
                obs::FieldView{"event_end", obs::Availability::known,
                               obs::Unit::emulation_frame, std::uint64_t{6U}, {}},
                obs::FieldView{"evaluated_frame", obs::Availability::known,
                               obs::Unit::emulation_frame, std::uint64_t{7U}, {}}};
            const auto causes = producer == 3U ? std::span<const obs::Cause>{detector_causes}
                                : producer == 4U
                                    ? std::span<const obs::Cause>{detector_causes}.first(2U)
                                : producer == 5U ? std::span<const obs::Cause>{selection_causes}
                                                 : std::span<const obs::Cause>{};
            const auto fields =
                producer == 3U   ? std::span<const obs::FieldView>{detector_fields}
                : producer == 4U ? std::span<const obs::FieldView>{detector_fields}.first(12U)
                : producer == 5U ? std::span<const obs::FieldView>{selection_fields}
                                 : std::span<const obs::FieldView>{};
            const std::array<obs::StateOrder, 1> detector_order{{"audio_detector_analysis", 1U}};
            const auto state_orders = producer == 4U
                                          ? std::span<const obs::StateOrder>{detector_order}
                                          : std::span<const obs::StateOrder>{};
            for (std::uint64_t sequence = 1U; sequence <= count; ++sequence) {
                production_observer.observe(obs::FactView{{producer, sequence},
                                                          kind,
                                                          {obs::Availability::known, 293U, {}},
                                                          causes,
                                                          state_orders,
                                                          fields});
            }
            ProductionSink production_sink{producer};
            while (production_bridge->try_consume_fact(&production_sink, ProductionSink::receive)) {
            }
            const auto production_metrics = production_bridge->metrics();
            require(production_sink.valid && production_sink.received == count &&
                        production_metrics.attempted_facts == count &&
                        production_metrics.enqueued_facts == count &&
                        production_metrics.consumed_facts == count &&
                        production_metrics.current_fact_occupancy == 0U &&
                        production_metrics.maximum_fact_occupancy == count &&
                        production_bridge->losses() == qa::ObservationBridgeLosses{},
                    failure);
        };
        verify_production_burst(3U, replay_detector_burst_count, "detector_burst_fact",
                                "production_detector_burst_was_not_lossless");
        verify_production_burst(4U, replay_detector_output_burst_count,
                                "detector_output_burst_fact",
                                "production_detector_output_burst_was_not_lossless");
        verify_production_burst(5U, replay_selection_burst_count, "selection_burst_fact",
                                "production_selection_burst_was_not_lossless");
        verify_production_burst(6U, replay_mixer_burst_count, "mixer_burst_fact",
                                "production_mixer_burst_was_not_lossless");

        auto balanced_bridge = std::make_unique<qa::ProductionObservationBridge>("run-balanced");
        const auto balanced_observer = balanced_bridge->observer();
        constexpr std::uint64_t regular_backlog = 27U;
        for (std::uint64_t sequence = 1U; sequence <= regular_backlog; ++sequence) {
            balanced_observer.observe(obs::FactView{
                {1U, sequence}, "regular_fact", {obs::Availability::known, 293U, {}}, {}, {}, {}});
        }
        for (std::uint64_t sequence = 1U; sequence <= replay_selection_burst_count; ++sequence) {
            balanced_observer.observe(obs::FactView{{5U, sequence},
                                                    "selection_fact",
                                                    {obs::Availability::known, 293U, {}},
                                                    {},
                                                    {},
                                                    {}});
        }
        BalancedSink balanced_sink;
        while (balanced_bridge->try_consume_fact(&balanced_sink, BalancedSink::receive)) {
        }
        require(balanced_sink.valid && balanced_sink.first_producer == 5U &&
                    balanced_sink.received[0] == regular_backlog &&
                    balanced_sink.received[4] == replay_selection_burst_count &&
                    balanced_bridge->losses() == qa::ObservationBridgeLosses{},
                "normalized_backlog_drain_was_not_lossless");

        auto fair_bridge = std::make_unique<qa::ProductionObservationBridge>("run-fair");
        const auto fair_observer = fair_bridge->observer();
        for (std::uint64_t sequence = 1U; sequence <= replay_detector_burst_count; ++sequence)
            fair_observer.observe(obs::FactView{{3U, sequence}, "detector", {}, {}, {}, {}});
        for (std::uint64_t sequence = 1U; sequence <= replay_selection_burst_count; ++sequence)
            fair_observer.observe(obs::FactView{{5U, sequence}, "selection", {}, {}, {}, {}});
        FairSink fair_sink;
        while (fair_bridge->try_consume_fact(&fair_sink, FairSink::receive)) {
        }
        require(fair_sink.received == replay_detector_burst_count + replay_selection_burst_count &&
                    fair_sink.first_run != 0U && fair_sink.first_run <= 1024U &&
                    fair_bridge->losses() == qa::ObservationBridgeLosses{},
                "full_burst_lanes_were_not_fairly_drained");

        std::printf("fact_burst_limits_test: burst=%llu elapsed_us=%llu "
                    "losses=0 max_queue=1 fact_limit=%llu batch_limit=%u "
                    "records=%llu\n",
                    static_cast<unsigned long long>(burst_fact_count),
                    static_cast<unsigned long long>(elapsed),
                    static_cast<unsigned long long>(qa::max_encoded_fact_bytes),
                    qa::max_protocol_payload_bytes,
                    static_cast<unsigned long long>(qa::max_fact_batch_records));
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "fact_burst_limits_test: %s\n", error.what());
        return 1;
    }
}
