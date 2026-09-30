#include "observation_bridge.h"

#include "fact_batch.h"
#include "protocol_header.h"
#include "replay_execution_result.h"

#include <ayther/engine/audio_observer.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace obs = ayther::engine::audio_observation;
namespace {

constexpr std::uint64_t facts_per_window = 50'000U;
constexpr std::uint64_t window_count = 60U;
constexpr std::uint64_t burst_fact_count = 100'000U;
constexpr std::uint64_t encoded_fact_bytes = 256U;

void require(const bool condition, const char *const message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Sink {
    std::uint64_t received{};
    std::uint64_t durable{};
    std::uint64_t expected_sequence{1U};
    bool valid{true};

    static void receive(void *const context, const std::string_view run_id,
                        const qa::EngineFactView &fact) noexcept {
        auto &sink = *static_cast<Sink *>(context);
        sink.valid = sink.valid && run_id == "run-throughput" && fact.id.producer == 1U &&
                     fact.id.sequence == sink.expected_sequence && fact.fields.size() == 1U &&
                     std::get<std::uint64_t>(fact.fields.front().value) == encoded_fact_bytes;
        ++sink.expected_sequence;
        ++sink.received;
        ++sink.durable;
    }
};

qa::Fact encoding_fixture(const std::uint64_t sequence, std::string kind) {
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

std::size_t encoded_record_bytes(const qa::Fact &fact) {
    const auto encoded = qa::encode_fact_batch(std::vector{fact}, 1U);
    const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
    require(message != nullptr, "valid_fact_could_not_be_encoded");
    return message->size() - qa::protocol_header_bytes - sizeof(std::uint32_t);
}

qa::Fact wire_fixture(const std::uint64_t sequence) {
    auto fact = encoding_fixture(sequence, "k");
    const auto base_size = encoded_record_bytes(fact);
    require(base_size <= encoded_fact_bytes, "compact_fact_exceeded_wire_budget");
    fact.kind.append(encoded_fact_bytes - base_size, 'k');
    require(encoded_record_bytes(fact) == encoded_fact_bytes,
            "padded_fact_did_not_match_wire_budget");

    const auto encoded = qa::encode_fact_batch(std::vector{fact}, 1U);
    const auto &message = std::get<std::vector<std::byte>>(encoded);
    const auto decoded = qa::decode_fact_batch(message, 1U);
    require(std::holds_alternative<std::vector<qa::Fact>>(decoded) &&
                std::get<std::vector<qa::Fact>>(decoded) == std::vector<qa::Fact>{fact},
            "compact_fact_round_trip_failed");
    return fact;
}

} // namespace

int main() {
    try {
        using Bridge = qa::RuntimeObservationBridge<1U, 128U, 1U>;
        auto bridge = std::make_unique<Bridge>("run-throughput");
        const auto observer = bridge->observer();
        require(observer.on_fact != nullptr, "fact_observer_unavailable");

        constexpr std::array representative_sequences{1U,      10U,      100U,      1'000U,
                                                      10'000U, 100'000U, 1'000'000U};
        std::array<qa::Fact, representative_sequences.size()> wire_fixtures;
        std::transform(representative_sequences.begin(), representative_sequences.end(),
                       wire_fixtures.begin(), wire_fixture);

        const std::array fields{obs::FieldView{
            "encoded_bytes", obs::Availability::known, obs::Unit::bytes, encoded_fact_bytes, {}}};
        Sink sink;
        std::uint64_t sequence{1U};
        std::uint64_t maximum_window_microseconds{};
        const auto sustained_started = std::chrono::steady_clock::now();

        for (std::uint64_t window{}; window < window_count; ++window) {
            const auto started = std::chrono::steady_clock::now();
            for (std::uint64_t index{}; index < facts_per_window; ++index, ++sequence) {
                observer.observe(obs::FactView{{1U, sequence},
                                               "throughput_fact",
                                               {obs::Availability::known, sequence, {}},
                                               {},
                                               {},
                                               fields});
                require(bridge->try_consume_fact(&sink, Sink::receive),
                        "enqueued_fact_was_not_received");
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count();
            require(elapsed >= 0 && elapsed <= 1'000'000, "sustained_window_exceeded_one_second");
            maximum_window_microseconds =
                std::max(maximum_window_microseconds, static_cast<std::uint64_t>(elapsed));
            std::this_thread::sleep_until(sustained_started + std::chrono::seconds(window + 1U));
        }

        const auto sustained_milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                  sustained_started)
                .count();
        require(sustained_milliseconds >= 60'000 && sustained_milliseconds <= 65'000,
                "sustained_duration_outside_contract");

        const auto burst_started = std::chrono::steady_clock::now();
        for (std::uint64_t index{}; index < burst_fact_count; ++index, ++sequence) {
            observer.observe(obs::FactView{{1U, sequence},
                                           "throughput_burst_fact",
                                           {obs::Availability::known, sequence, {}},
                                           {},
                                           {},
                                           fields});
            require(bridge->try_consume_fact(&sink, Sink::receive),
                    "post_sustained_burst_fact_was_not_received");
        }
        const auto burst_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                            std::chrono::steady_clock::now() - burst_started)
                                            .count();
        require(burst_microseconds >= 0 && burst_microseconds <= 1'000'000,
                "post_sustained_burst_exceeded_one_second");

        while (sequence <= qa::max_replay_trace_facts) {
            observer.observe(obs::FactView{{1U, sequence},
                                           "execution_limit_fact",
                                           {obs::Availability::known, sequence, {}},
                                           {},
                                           {},
                                           fields});
            require(bridge->try_consume_fact(&sink, Sink::receive),
                    "execution_limit_fact_was_not_received");
            ++sequence;
        }

        constexpr std::uint64_t expected = qa::max_replay_trace_facts;
        const auto metrics = bridge->metrics();
        const auto losses = bridge->losses();
        require(sink.valid && sink.received == expected && sink.durable == expected &&
                    metrics.attempted_facts == expected && metrics.enqueued_facts == expected &&
                    metrics.consumed_facts == expected && metrics.current_fact_occupancy == 0U &&
                    metrics.maximum_fact_occupancy == 1U && losses == qa::ObservationBridgeLosses{},
                "sustained_fact_transport_counters_differed");
        require(qa::replay_trace_fact_count_within_limit(expected) &&
                    !qa::replay_trace_fact_count_within_limit(expected + 1U),
                "execution_fact_limit_boundary_changed");

        std::printf("fact_throughput_test: windows=%llu facts_per_window=%llu "
                    "attempted=%llu enqueued=%llu received=%llu durable=%llu "
                    "losses=0 max_queue=%llu max_window_us=%llu burst=%llu "
                    "burst_us=%llu execution_limit=%llu "
                    "wire_fact_bytes=%llu schema=1.1 duration_ms=%llu\n",
                    static_cast<unsigned long long>(window_count),
                    static_cast<unsigned long long>(facts_per_window),
                    static_cast<unsigned long long>(metrics.attempted_facts),
                    static_cast<unsigned long long>(metrics.enqueued_facts),
                    static_cast<unsigned long long>(sink.received),
                    static_cast<unsigned long long>(sink.durable),
                    static_cast<unsigned long long>(metrics.maximum_fact_occupancy),
                    static_cast<unsigned long long>(maximum_window_microseconds),
                    static_cast<unsigned long long>(burst_fact_count),
                    static_cast<unsigned long long>(burst_microseconds),
                    static_cast<unsigned long long>(expected),
                    static_cast<unsigned long long>(encoded_fact_bytes),
                    static_cast<unsigned long long>(sustained_milliseconds));
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "fact_throughput_test: %s\n", error.what());
        return 1;
    }
}
