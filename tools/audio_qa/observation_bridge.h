#pragma once

#include "model_limits.h"

#include <ayther/engine/audio_fact_queue.hpp>
#include <ayther/engine/audio_pcm_queue.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace ayther::audio_qa {

using EngineFactView = ayther::engine::audio_observation::FactView;
using EnginePcmView = ayther::engine::audio_observation::PcmView;
using EngineObserver = ayther::engine::audio_observation::Observer;

using RuntimeFactConsumer = void (*)(void *, std::string_view, const EngineFactView &) noexcept;
using RuntimePcmConsumer = void (*)(void *, std::string_view, const EnginePcmView &) noexcept;

struct ObservationBridgeLosses {
    std::uint64_t invalid_producer{};
    std::uint64_t invalid_fact{};
    std::uint64_t full_fact{};
    std::uint64_t closed_fact{};
    std::uint64_t invalid_pcm{};
    std::uint64_t full_pcm{};
    std::uint64_t closed_pcm{};
    bool operator==(const ObservationBridgeLosses &) const = default;
};

struct ObservationBridgeMetrics {
    std::uint64_t attempted_facts{};
    std::uint64_t enqueued_facts{};
    std::uint64_t consumed_facts{};
    std::uint64_t current_fact_occupancy{};
    std::uint64_t maximum_fact_occupancy{};
    bool operator==(const ObservationBridgeMetrics &) const = default;
};

// One fixed SPSC queue is reserved per Engine producer identity. A producer
// with a larger measured per-step burst may use a dedicated queue without
// multiplying that capacity across every producer. Producers with distinct
// identities may call concurrently; the Runtime consumer is single. Delivery
// views borrow queue storage only for the synchronous consumer call.
template <std::size_t ProducerSlots, std::size_t FactCapacity, std::size_t PcmCapacity,
          std::size_t BurstProducer = 0U, std::size_t BurstFactCapacity = 1U,
          std::size_t SecondBurstProducer = 0U, std::size_t SecondBurstFactCapacity = 1U,
          std::size_t BurstPcmProducer = 0U, std::size_t BurstPcmCapacity = 1U,
          std::size_t ThirdBurstProducer = 0U, std::size_t ThirdBurstFactCapacity = 1U,
          std::size_t BurstPcmBytes = ayther::engine::audio_observation::max_pcm_bytes,
          std::size_t FourthBurstProducer = 0U, std::size_t FourthBurstFactCapacity = 1U>
class RuntimeObservationBridge final {
    static_assert(ProducerSlots > 0 && FactCapacity > 0 && PcmCapacity > 0 &&
                  BurstFactCapacity > 0 && SecondBurstFactCapacity > 0 && BurstPcmCapacity > 0 &&
                  ThirdBurstFactCapacity > 0 && FourthBurstFactCapacity > 0 && BurstPcmBytes > 0 &&
                  BurstPcmBytes <= ayther::engine::audio_observation::max_pcm_bytes &&
                  BurstProducer <= ProducerSlots && SecondBurstProducer <= ProducerSlots &&
                  BurstPcmProducer <= ProducerSlots && ThirdBurstProducer <= ProducerSlots &&
                  FourthBurstProducer <= ProducerSlots &&
                  (BurstProducer == 0U || SecondBurstProducer == 0U ||
                   BurstProducer != SecondBurstProducer) &&
                  (BurstProducer == 0U || ThirdBurstProducer == 0U ||
                   BurstProducer != ThirdBurstProducer) &&
                  (SecondBurstProducer == 0U || ThirdBurstProducer == 0U ||
                   SecondBurstProducer != ThirdBurstProducer) &&
                  (BurstProducer == 0U || FourthBurstProducer == 0U ||
                   BurstProducer != FourthBurstProducer) &&
                  (SecondBurstProducer == 0U || FourthBurstProducer == 0U ||
                   SecondBurstProducer != FourthBurstProducer) &&
                  (ThirdBurstProducer == 0U || FourthBurstProducer == 0U ||
                   ThirdBurstProducer != FourthBurstProducer));

    using FactQueue = ayther::engine::audio_observation::BoundedFactQueue<FactCapacity>;
    // Selection facts share one session-thread producer. Their schemas use at
    // most four causes, no state-order entries and 11 fields. A 12-field,
    // 1024-byte slot preserves that schema while leaving enough fixed storage
    // for the measured full-take backlog without multiplying the regular queue
    // footprint.
    using BurstFactQueue =
        ayther::engine::audio_observation::BoundedFactQueue<BurstFactCapacity, 4U, 0U, 12U, 1024U>;
    // The second dedicated lane is the detector-input producer in the
    // production bridge. Detector-input and source-interval facts have at
    // most three causes, no state-order entries and 24 fields. The 1536-byte
    // text budget covers their fixed schema strings while retaining the
    // measured 4096-fact burst inside the fixed queue-memory budget.
    using SecondBurstFactQueue =
        ayther::engine::audio_observation::BoundedFactQueue<SecondBurstFactCapacity, 3U, 0U, 24U,
                                                            1536U>;
    using ThirdBurstFactQueue =
        ayther::engine::audio_observation::BoundedFactQueue<ThirdBurstFactCapacity>;
    // Detector output facts have at most two causes, one state-order entry and
    // nine fields. A dedicated compact lane absorbs the measured replay
    // backlog without multiplying the regular producer capacity.
    using FourthBurstFactQueue =
        ayther::engine::audio_observation::BoundedFactQueue<FourthBurstFactCapacity, 2U, 1U, 12U,
                                                            1024U>;
    using PcmQueue = ayther::engine::audio_observation::BoundedPcmQueue<PcmCapacity>;
    using BurstPcmQueue =
        ayther::engine::audio_observation::BoundedPcmQueue<BurstPcmCapacity, BurstPcmBytes>;
    static constexpr std::size_t dedicated_fact_queues =
        (BurstProducer == 0U ? 0U : 1U) + (SecondBurstProducer == 0U ? 0U : 1U) +
        (ThirdBurstProducer == 0U ? 0U : 1U) + (FourthBurstProducer == 0U ? 0U : 1U);
    static constexpr std::size_t dedicated_pcm_queues = BurstPcmProducer == 0U ? 0U : 1U;
    struct FactSlot {
        FactQueue queue{nullptr};
    };
    struct PcmSlot {
        PcmQueue queue{nullptr};
    };

  public:
    explicit RuntimeObservationBridge(const std::string_view run_id) noexcept {
        if (!run_id.empty() && run_id.size() <= max_identity_bytes) {
            std::memcpy(run_id_.data(), run_id.data(), run_id.size());
            run_id_size_ = run_id.size();
        }
    }

    RuntimeObservationBridge(const RuntimeObservationBridge &) = delete;
    RuntimeObservationBridge &operator=(const RuntimeObservationBridge &) = delete;
    RuntimeObservationBridge(RuntimeObservationBridge &&) = delete;
    RuntimeObservationBridge &operator=(RuntimeObservationBridge &&) = delete;

    [[nodiscard]] bool valid() const noexcept { return run_id_size_ != 0; }
    [[nodiscard]] std::string_view run_id() const noexcept {
        return {run_id_.data(), run_id_size_};
    }

    [[nodiscard]] EngineObserver observer() noexcept {
        if (!valid())
            return {};
        return {this, receive_fact, receive_pcm};
    }

    [[nodiscard]] bool try_consume_fact(void *context, RuntimeFactConsumer consumer) noexcept {
        if (!valid() || consumer == nullptr)
            return false;
        FactDelivery delivery{this, context, consumer};

        // Drain a bounded run from the previously selected producer before
        // scanning every lane again. Replay frames commonly publish hundreds
        // of consecutive facts from one producer; rescanning all nine atomic
        // occupancies for every item adds avoidable work to the frame thread.
        // The run limit forces a fresh normalized-occupancy decision often
        // enough to preserve progress for lower-volume producers.
        constexpr std::size_t maximum_drain_run = 4096U;
        if (drain_fact_ < ProducerSlots && drain_fact_count_ < maximum_drain_run &&
            current_fact_occupancy_by_producer_[drain_fact_].load(std::memory_order_acquire) !=
                0U &&
            try_consume_fact_at(drain_fact_, delivery)) {
            consumed_fact_.fetch_add(1, std::memory_order_relaxed);
            current_fact_occupancy_.fetch_sub(1, std::memory_order_relaxed);
            current_fact_occupancy_by_producer_[drain_fact_].fetch_sub(1,
                                                                       std::memory_order_release);
            ++drain_fact_count_;
            return true;
        }
        drain_fact_ = ProducerSlots;
        drain_fact_count_ = 0U;

        // Prefer the producer closest to exhausting its own fixed queue. The
        // producers have very different capacities and sustained rates, so a
        // one-item round robin can starve a high-volume lane even while the
        // consumer has enough aggregate throughput. Ties retain round-robin
        // order. Per-producer FIFO order remains unchanged.
        std::size_t selected = ProducerSlots;
        std::uint64_t selected_occupancy{};
        std::size_t selected_capacity{1U};
        for (std::size_t offset = 0; offset < ProducerSlots; ++offset) {
            const auto index = (next_fact_ + offset) % ProducerSlots;
            const auto occupancy =
                current_fact_occupancy_by_producer_[index].load(std::memory_order_acquire);
            const auto capacity = fact_capacity(index);
            if (occupancy != 0U &&
                (selected == ProducerSlots ||
                 occupancy * selected_capacity > selected_occupancy * capacity)) {
                selected = index;
                selected_occupancy = occupancy;
                selected_capacity = capacity;
            }
        }
        if (selected != ProducerSlots && try_consume_fact_at(selected, delivery)) {
            consumed_fact_.fetch_add(1, std::memory_order_relaxed);
            current_fact_occupancy_.fetch_sub(1, std::memory_order_relaxed);
            current_fact_occupancy_by_producer_[selected].fetch_sub(1, std::memory_order_release);
            next_fact_ = (selected + 1) % ProducerSlots;
            drain_fact_ = selected;
            drain_fact_count_ = 1U;
            return true;
        }

        // An occupancy is reserved immediately before publication. If the
        // consumer observes that reservation in the narrow pre-publication
        // window, fall back to the other queues instead of reporting idle.
        for (std::size_t offset = 0; offset < ProducerSlots; ++offset) {
            const auto index = (next_fact_ + offset) % ProducerSlots;
            if (index != selected && try_consume_fact_at(index, delivery)) {
                consumed_fact_.fetch_add(1, std::memory_order_relaxed);
                current_fact_occupancy_.fetch_sub(1, std::memory_order_relaxed);
                current_fact_occupancy_by_producer_[index].fetch_sub(1, std::memory_order_release);
                next_fact_ = (index + 1) % ProducerSlots;
                drain_fact_ = index;
                drain_fact_count_ = 1U;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool try_consume_pcm(void *context, RuntimePcmConsumer consumer) noexcept {
        if (!valid() || consumer == nullptr)
            return false;
        PcmDelivery delivery{this, context, consumer};
        for (std::size_t offset = 0; offset < ProducerSlots; ++offset) {
            const auto index = (next_pcm_ + offset) % ProducerSlots;
            const bool consumed = [&] {
                if constexpr (BurstPcmProducer != 0U) {
                    if (index == BurstPcmProducer - 1U)
                        return burst_pcm_queue_.try_consume(&delivery, deliver_pcm);
                }
                return pcm_queues_[regular_pcm_index(index)].queue.try_consume(&delivery,
                                                                               deliver_pcm);
            }();
            if (consumed) {
                next_pcm_ = (index + 1) % ProducerSlots;
                return true;
            }
        }
        return false;
    }

    void close() noexcept {
        for (auto &queue : fact_queues_)
            queue.queue.close();
        if constexpr (BurstProducer != 0U)
            burst_fact_queue_.close();
        if constexpr (SecondBurstProducer != 0U)
            second_burst_fact_queue_.close();
        if constexpr (ThirdBurstProducer != 0U)
            third_burst_fact_queue_.close();
        if constexpr (FourthBurstProducer != 0U)
            fourth_burst_fact_queue_.close();
        for (auto &queue : pcm_queues_)
            queue.queue.close();
        if constexpr (BurstPcmProducer != 0U)
            burst_pcm_queue_.close();
    }

    [[nodiscard]] ObservationBridgeLosses losses() const noexcept {
        return {invalid_producer_.load(std::memory_order_relaxed),
                invalid_fact_.load(std::memory_order_relaxed),
                full_fact_.load(std::memory_order_relaxed),
                closed_fact_.load(std::memory_order_relaxed),
                invalid_pcm_.load(std::memory_order_relaxed),
                full_pcm_.load(std::memory_order_relaxed),
                closed_pcm_.load(std::memory_order_relaxed)};
    }

    [[nodiscard]] ObservationBridgeMetrics metrics() const noexcept {
        return {attempted_fact_.load(std::memory_order_relaxed),
                enqueued_fact_.load(std::memory_order_relaxed),
                consumed_fact_.load(std::memory_order_relaxed),
                current_fact_occupancy_.load(std::memory_order_relaxed),
                maximum_fact_occupancy_.load(std::memory_order_relaxed)};
    }

    [[nodiscard]] std::uint32_t first_full_fact_producer() const noexcept {
        for (std::size_t index{}; index < ProducerSlots; ++index) {
            if (full_fact_by_producer_[index].load(std::memory_order_relaxed) != 0U)
                return static_cast<std::uint32_t>(index + 1U);
        }
        return 0U;
    }

  private:
    struct FactDelivery {
        RuntimeObservationBridge *bridge;
        void *context;
        RuntimeFactConsumer consumer;
    };
    struct PcmDelivery {
        RuntimeObservationBridge *bridge;
        void *context;
        RuntimePcmConsumer consumer;
    };

    [[nodiscard]] static constexpr std::size_t fact_capacity(const std::size_t index) noexcept {
        if constexpr (BurstProducer != 0U) {
            if (index == BurstProducer - 1U)
                return BurstFactCapacity;
        }
        if constexpr (SecondBurstProducer != 0U) {
            if (index == SecondBurstProducer - 1U)
                return SecondBurstFactCapacity;
        }
        if constexpr (ThirdBurstProducer != 0U) {
            if (index == ThirdBurstProducer - 1U)
                return ThirdBurstFactCapacity;
        }
        if constexpr (FourthBurstProducer != 0U) {
            if (index == FourthBurstProducer - 1U)
                return FourthBurstFactCapacity;
        }
        return FactCapacity;
    }

    [[nodiscard]] bool try_consume_fact_at(const std::size_t index,
                                           FactDelivery &delivery) noexcept {
        if constexpr (BurstProducer != 0U) {
            if (index == BurstProducer - 1U)
                return burst_fact_queue_.try_consume(&delivery, deliver_fact);
        }
        if constexpr (SecondBurstProducer != 0U) {
            if (index == SecondBurstProducer - 1U)
                return second_burst_fact_queue_.try_consume(&delivery, deliver_fact);
        }
        if constexpr (ThirdBurstProducer != 0U) {
            if (index == ThirdBurstProducer - 1U)
                return third_burst_fact_queue_.try_consume(&delivery, deliver_fact);
        }
        if constexpr (FourthBurstProducer != 0U) {
            if (index == FourthBurstProducer - 1U)
                return fourth_burst_fact_queue_.try_consume(&delivery, deliver_fact);
        }
        return fact_queues_[regular_fact_index(index)].queue.try_consume(&delivery, deliver_fact);
    }

    [[nodiscard]] static std::size_t producer_index(const std::uint32_t producer) noexcept {
        return static_cast<std::size_t>(producer - 1U);
    }

    [[nodiscard]] static constexpr std::size_t
    regular_fact_index(const std::size_t producer) noexcept {
        std::size_t index = producer;
        if constexpr (BurstProducer != 0U) {
            if (producer > BurstProducer - 1U)
                --index;
        }
        if constexpr (SecondBurstProducer != 0U) {
            if (producer > SecondBurstProducer - 1U)
                --index;
        }
        if constexpr (ThirdBurstProducer != 0U) {
            if (producer > ThirdBurstProducer - 1U)
                --index;
        }
        if constexpr (FourthBurstProducer != 0U) {
            if (producer > FourthBurstProducer - 1U)
                --index;
        }
        return index;
    }

    [[nodiscard]] static constexpr std::size_t
    regular_pcm_index(const std::size_t producer) noexcept {
        if constexpr (BurstPcmProducer != 0U) {
            return producer > BurstPcmProducer - 1U ? producer - 1U : producer;
        } else {
            return producer;
        }
    }

    static void receive_fact(void *context, const EngineFactView &fact) noexcept {
        auto &bridge = *static_cast<RuntimeObservationBridge *>(context);
        bridge.attempted_fact_.fetch_add(1, std::memory_order_relaxed);
        if (fact.id.producer == 0 || fact.id.producer > ProducerSlots) {
            bridge.invalid_producer_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        auto &producer_occupancy =
            bridge.current_fact_occupancy_by_producer_[producer_index(fact.id.producer)];
        producer_occupancy.fetch_add(1, std::memory_order_acq_rel);
        const auto occupancy =
            bridge.current_fact_occupancy_.fetch_add(1, std::memory_order_relaxed) + 1U;
        using Result = ayther::engine::audio_observation::FactPushResult;
        const auto result = [&] {
            if constexpr (BurstProducer != 0U) {
                if (fact.id.producer == BurstProducer)
                    return bridge.burst_fact_queue_.try_push(fact);
            }
            if constexpr (SecondBurstProducer != 0U) {
                if (fact.id.producer == SecondBurstProducer)
                    return bridge.second_burst_fact_queue_.try_push(fact);
            }
            if constexpr (ThirdBurstProducer != 0U) {
                if (fact.id.producer == ThirdBurstProducer)
                    return bridge.third_burst_fact_queue_.try_push(fact);
            }
            if constexpr (FourthBurstProducer != 0U) {
                if (fact.id.producer == FourthBurstProducer)
                    return bridge.fourth_burst_fact_queue_.try_push(fact);
            }
            return bridge.fact_queues_[regular_fact_index(producer_index(fact.id.producer))]
                .queue.try_push(fact);
        }();
        switch (result) {
        case Result::accepted:
            bridge.enqueued_fact_.fetch_add(1, std::memory_order_relaxed);
            bridge.update_maximum_fact_occupancy(occupancy);
            break;
        case Result::full:
            bridge.current_fact_occupancy_.fetch_sub(1, std::memory_order_relaxed);
            producer_occupancy.fetch_sub(1, std::memory_order_release);
            bridge.full_fact_.fetch_add(1, std::memory_order_relaxed);
            bridge.full_fact_by_producer_[producer_index(fact.id.producer)].fetch_add(
                1U, std::memory_order_relaxed);
            break;
        case Result::invalid:
            bridge.current_fact_occupancy_.fetch_sub(1, std::memory_order_relaxed);
            producer_occupancy.fetch_sub(1, std::memory_order_release);
            bridge.invalid_fact_.fetch_add(1, std::memory_order_relaxed);
            break;
        case Result::closed:
            bridge.current_fact_occupancy_.fetch_sub(1, std::memory_order_relaxed);
            producer_occupancy.fetch_sub(1, std::memory_order_release);
            bridge.closed_fact_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }

    void update_maximum_fact_occupancy(const std::uint64_t occupancy) noexcept {
        auto maximum = maximum_fact_occupancy_.load(std::memory_order_relaxed);
        while (maximum < occupancy &&
               !maximum_fact_occupancy_.compare_exchange_weak(
                   maximum, occupancy, std::memory_order_relaxed, std::memory_order_relaxed)) {
        }
    }

    static void receive_pcm(void *context, const EnginePcmView &pcm) noexcept {
        auto &bridge = *static_cast<RuntimeObservationBridge *>(context);
        if (pcm.id.producer == 0 || pcm.id.producer > ProducerSlots) {
            bridge.invalid_producer_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        using Result = ayther::engine::audio_observation::PcmPushResult;
        const auto result = [&] {
            if constexpr (BurstPcmProducer != 0U) {
                if (pcm.id.producer == BurstPcmProducer)
                    return bridge.burst_pcm_queue_.try_push(pcm);
            }
            return bridge.pcm_queues_[regular_pcm_index(producer_index(pcm.id.producer))]
                .queue.try_push(pcm);
        }();
        switch (result) {
        case Result::accepted:
            break;
        case Result::full:
            bridge.full_pcm_.fetch_add(1, std::memory_order_relaxed);
            break;
        case Result::invalid:
            bridge.invalid_pcm_.fetch_add(1, std::memory_order_relaxed);
            break;
        case Result::closed:
            bridge.closed_pcm_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }

    static void deliver_fact(void *context, const EngineFactView &fact) noexcept {
        auto &delivery = *static_cast<FactDelivery *>(context);
        delivery.consumer(delivery.context, delivery.bridge->run_id(), fact);
    }

    static void deliver_pcm(void *context, const EnginePcmView &pcm) noexcept {
        auto &delivery = *static_cast<PcmDelivery *>(context);
        delivery.consumer(delivery.context, delivery.bridge->run_id(), pcm);
    }

    std::array<char, max_identity_bytes> run_id_{};
    std::size_t run_id_size_{};
    std::array<FactSlot, ProducerSlots - dedicated_fact_queues> fact_queues_{};
    BurstFactQueue burst_fact_queue_{nullptr};
    SecondBurstFactQueue second_burst_fact_queue_{nullptr};
    ThirdBurstFactQueue third_burst_fact_queue_{nullptr};
    FourthBurstFactQueue fourth_burst_fact_queue_{nullptr};
    std::array<PcmSlot, ProducerSlots - dedicated_pcm_queues> pcm_queues_{};
    BurstPcmQueue burst_pcm_queue_{nullptr};
    std::size_t next_fact_{};
    std::size_t drain_fact_{ProducerSlots};
    std::size_t drain_fact_count_{};
    std::size_t next_pcm_{};
    std::atomic<std::uint64_t> invalid_producer_{0};
    std::atomic<std::uint64_t> invalid_fact_{0};
    std::atomic<std::uint64_t> full_fact_{0};
    std::array<std::atomic<std::uint64_t>, ProducerSlots> full_fact_by_producer_{};
    std::atomic<std::uint64_t> closed_fact_{0};
    std::atomic<std::uint64_t> invalid_pcm_{0};
    std::atomic<std::uint64_t> full_pcm_{0};
    std::atomic<std::uint64_t> closed_pcm_{0};
    std::atomic<std::uint64_t> attempted_fact_{0};
    std::atomic<std::uint64_t> enqueued_fact_{0};
    std::atomic<std::uint64_t> consumed_fact_{0};
    std::atomic<std::uint64_t> current_fact_occupancy_{0};
    std::array<std::atomic<std::uint64_t>, ProducerSlots> current_fact_occupancy_by_producer_{};
    std::atomic<std::uint64_t> maximum_fact_occupancy_{0};
};

using ProductionObservationBridge = RuntimeObservationBridge<9U, 54U, 1U, 5U, 4096U, 3U, 4096U, 7U,
                                                             2048U, 6U, 384U, 1024U, 4U, 1024U>;

} // namespace ayther::audio_qa
