#pragma once

#include "checkpoint_ring.h"

#include <ayther/ayther_session.h>
#include <ayther/engine/audio_hd_state.hpp>
#include <ayther/engine/audio_observer.hpp>
#include <ayther/engine/visual_state.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace ayther::runtime {

// Spec 002, BR-156 (plan §8 P-9; DI-12): where each frame starts on the device output line
// (`audio_frame_output_boundary`) and when each postmix PCM block of that line reached the audio
// device, on the steady clock of the Runtime. The device threads only append to preallocated
// slots; the session reads them once every producer has stopped.
struct AudioTimingTap {
    static constexpr std::size_t capacity = std::size_t{1} << 16U;
    struct Boundary {
        double at_ms{};
        std::uint64_t emulation_frame{};
        std::uint64_t output_position{};
        std::uint32_t sample_rate{};
        bool valid{};
    };
    struct Block {
        double at_ms{};
        std::uint64_t begin{};
        std::uint64_t end{};
        std::uint32_t sample_rate{};
    };
    std::vector<Boundary> boundaries = std::vector<Boundary>(capacity);
    std::vector<Block> blocks = std::vector<Block>(capacity);
    std::atomic<std::size_t> boundary_count{};
    std::atomic<std::size_t> block_count{};
};

// Spec 002 (contracts.md C4 with DI-8; plan D14): the Runtime knows when it produces in
// silent mode and keeps those facts and that PCM out of the linear evidence at its sink.
// The gate wraps the evidence observer; the Engine receives the gate.
class SilentProductionGate final {
  public:
    explicit SilentProductionGate(engine::audio_observation::Observer inner) noexcept
        : inner_(inner) {}
    SilentProductionGate(const SilentProductionGate &) = delete;
    SilentProductionGate &operator=(const SilentProductionGate &) = delete;

    [[nodiscard]] engine::audio_observation::Observer observer() noexcept {
        return {this, receive_fact, inner_.on_pcm != nullptr ? receive_pcm : nullptr};
    }
    [[nodiscard]] engine::audio_observation::PcmCallback pcm_callback() const noexcept {
        return inner_.on_pcm != nullptr ? receive_pcm : nullptr;
    }
    void set_silent(bool silent) noexcept { silent_.store(silent, std::memory_order_release); }
    // BR-156: set before the session exists, read after it is destroyed.
    void set_timing_tap(AudioTimingTap *tap) noexcept { tap_ = tap; }
    [[nodiscard]] bool silent() const noexcept { return silent_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t excluded_facts() const noexcept {
        return excluded_facts_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t excluded_pcm() const noexcept {
        return excluded_pcm_.load(std::memory_order_acquire);
    }
    // Spec 002, DI-14: the PCM blocks that reached the evidence observer, in order. The sink
    // consumes them in the same order, so this count marks where a segment begins.
    [[nodiscard]] std::uint64_t forwarded_pcm() const noexcept {
        return forwarded_pcm_.load(std::memory_order_acquire);
    }

  private:
    static void receive_fact(void *context,
                             const engine::audio_observation::FactView &fact) noexcept;
    static void receive_pcm(void *context, const engine::audio_observation::PcmView &pcm) noexcept;

    engine::audio_observation::Observer inner_;
    AudioTimingTap *tap_{};
    std::atomic<bool> silent_{};
    std::atomic<std::uint64_t> excluded_facts_{};
    std::atomic<std::uint64_t> excluded_pcm_{};
    std::atomic<std::uint64_t> forwarded_pcm_{};
};

// Spec 002, plan §4.4 and §5.6 (RF-3.6, RF-5.2, RF-5.6; P-12): the checkpoints of a take.
// A checkpoint is the state after producing its frame: the core, the visual state (C4) and
// the HD audio state. The pure CheckpointRing decides what is kept; the PCM of the HD assets
// is shared between checkpoints, since every checkpoint of a take refers to the same assets.
class CheckpointStore final {
  public:
    enum class RestoreResult {
        restored,
        missing,
        core_rejected,
        visual_rejected,
        audio_detector_rejected,
        audio_voices_rejected,
    };

    CheckpointStore(std::uint32_t interval, std::uint64_t budget_bytes) noexcept
        : ring_(interval, budget_bytes) {}

    // The state after producing `frame`; frame −1 is the initial state, always kept.
    [[nodiscard]] bool capture(const AytherSession &session, std::int64_t frame);
    // `audio` false keeps a rejection of the HD audio state from failing the restore: a
    // post-end inspection is silent and its re-armed session does not take the pending audio
    // of the take (RF-2.9).
    [[nodiscard]] RestoreResult restore(AytherSession &session, std::int64_t frame,
                                        bool audio = true) const;
    [[nodiscard]] const replay_inspection::CheckpointRing &ring() const noexcept { return ring_; }
    [[nodiscard]] bool should_capture(std::uint32_t frame) const noexcept {
        return ring_.should_capture(frame);
    }
    // BR-143: a test damages the stored checkpoints to exercise the recovery failure.
    // A restore went on without the pending HD audio, which the Engine rejected.
    [[nodiscard]] bool pending_audio_not_restored() const noexcept {
        return pending_audio_not_restored_;
    }
    void corrupt_all_for_test() noexcept;
    void corrupt_visual_states_for_test() noexcept;

  private:
    struct Checkpoint {
        std::vector<std::uint8_t> core_state;
        engine::visual_state::VisualState visual;
        engine::audio_observation::AudioHdStateHeader audio_header;
        engine::audio_observation::AudioHdDetectorWindowsState detector_windows;
        engine::audio_observation::AudioHdVoicesState voices;
        engine::audio_observation::AudioHdRequestsPendingState requests;
        std::vector<std::pair<std::uint64_t, std::shared_ptr<const std::vector<std::int16_t>>>>
            shared_pcm;
    };

    replay_inspection::CheckpointRing ring_;
    std::map<std::int64_t, Checkpoint> checkpoints_;
    mutable bool pending_audio_not_restored_{};
    std::map<std::pair<std::uint64_t, std::size_t>,
             std::shared_ptr<const std::vector<std::int16_t>>>
        pcm_pool_;
};

} // namespace ayther::runtime
