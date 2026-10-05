#include "audio_qa_inspection.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <variant>

namespace ayther::runtime {
namespace {

namespace audio = ayther::engine::audio_observation;

double steady_ms() noexcept {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

template <class T> std::uint64_t bytes_of(const std::vector<T> &values) noexcept {
    return static_cast<std::uint64_t>(values.size()) * sizeof(T);
}

std::uint64_t pending_bytes(const audio::AudioHdRequestsPendingState &state) noexcept {
    return bytes_of(state.requests) + bytes_of(state.fired_requests) +
           bytes_of(state.pending_audio.main_pcm) + bytes_of(state.pending_audio.main_batches) +
           bytes_of(state.pending_audio.original_batches) +
           bytes_of(state.pending_audio.frame_mute_hashes) +
           bytes_of(state.pending_audio.user_mute_hashes);
}

} // namespace

void SilentProductionGate::receive_fact(void *context, const audio::FactView &fact) noexcept {
    auto &gate = *static_cast<SilentProductionGate *>(context);
    if (gate.tap_ != nullptr && fact.kind == "audio_frame_output_boundary") {
        AudioTimingTap::Boundary boundary{steady_ms(), fact.frame.emulation_frame, 0, 0, false};
        for (const auto &field : fact.fields) {
            if (field.name == "output_position")
                if (const auto *value = std::get_if<std::uint64_t>(&field.value))
                    boundary.output_position = *value;
            if (field.name == "sample_rate")
                if (const auto *value = std::get_if<std::uint64_t>(&field.value))
                    boundary.sample_rate = static_cast<std::uint32_t>(*value);
            if (field.name == "valid")
                if (const auto *value = std::get_if<bool>(&field.value))
                    boundary.valid = *value;
        }
        const auto index = gate.tap_->boundary_count.fetch_add(1, std::memory_order_acq_rel);
        if (index < AudioTimingTap::capacity)
            gate.tap_->boundaries[index] = boundary;
    }
    if (gate.silent()) {
        gate.excluded_facts_.fetch_add(1, std::memory_order_acq_rel);
        return;
    }
    gate.inner_.observe(fact);
}

void SilentProductionGate::receive_pcm(void *context, const audio::PcmView &pcm) noexcept {
    auto &gate = *static_cast<SilentProductionGate *>(context);
    if (gate.tap_ != nullptr && pcm.capture_point == "sdl_logical_device_postmix") {
        const auto index = gate.tap_->block_count.fetch_add(1, std::memory_order_acq_rel);
        if (index < AudioTimingTap::capacity)
            gate.tap_->blocks[index] = {steady_ms(), pcm.range.begin, pcm.range.end,
                                        pcm.range.sample_rate};
    }
    if (gate.silent()) {
        gate.excluded_pcm_.fetch_add(1, std::memory_order_acq_rel);
        return;
    }
    gate.forwarded_pcm_.fetch_add(1, std::memory_order_acq_rel);
    gate.inner_.observe(pcm);
}

bool CheckpointStore::capture(const AytherSession &session, const std::int64_t frame) {
    try {
        Checkpoint checkpoint;
        if (!session.serialize(checkpoint.core_state) || checkpoint.core_state.empty())
            return false;
        checkpoint.visual = session.export_visual_state();
        const auto identity = session.game_state_identity();
        if (identity.empty() || checkpoint.visual.header.game_state_identity != identity)
            return false;
        checkpoint.audio_header.game_state_identity = identity;
        checkpoint.audio_header.emulation_frame = checkpoint.visual.header.emulation_frame;
        checkpoint.audio_header.sections = audio::kAudioHdStateRequiredSections;
        checkpoint.detector_windows = session.audio_hd_detector_windows_state();
        checkpoint.voices = session.audio_hd_voices_state();
        checkpoint.requests = session.audio_hd_requests_pending_state();

        // The PCM of the assets is the same for every checkpoint of the take: it is kept once.
        std::uint64_t size = bytes_of(checkpoint.core_state) + bytes_of(checkpoint.visual.payload) +
                             bytes_of(checkpoint.detector_windows.detector) +
                             bytes_of(checkpoint.detector_windows.windows) +
                             bytes_of(checkpoint.voices.voices) +
                             pending_bytes(checkpoint.requests);
        for (auto &asset : checkpoint.voices.pcm_assets) {
            const auto key = std::make_pair(asset.identity, asset.samples.size());
            auto &pooled = pcm_pool_[key];
            if (!pooled || *pooled != asset.samples) {
                size += bytes_of(asset.samples);
                pooled =
                    std::make_shared<const std::vector<std::int16_t>>(std::move(asset.samples));
            }
            checkpoint.shared_pcm.emplace_back(asset.identity, pooled);
            asset.samples.clear();
        }
        if (frame == replay_inspection::initial_checkpoint_frame) {
            ring_.store_initial(size);
        } else if (frame < 0 || !ring_.store(static_cast<std::uint32_t>(frame), size)) {
            return false;
        }
        checkpoints_[frame] = std::move(checkpoint);
        // Keep exactly what the ring kept.
        for (auto entry = checkpoints_.begin(); entry != checkpoints_.end();) {
            const bool kept = std::any_of(
                ring_.entries().begin(), ring_.entries().end(),
                [&entry](const auto &kept_entry) { return kept_entry.frame == entry->first; });
            entry = kept ? std::next(entry) : checkpoints_.erase(entry);
        }
        return true;
    } catch (...) {
        return false;
    }
}

CheckpointStore::RestoreResult
CheckpointStore::restore(AytherSession &session, const std::int64_t frame, const bool audio) const {
    const auto found = checkpoints_.find(frame);
    if (found == checkpoints_.end())
        return RestoreResult::missing;
    const auto &checkpoint = found->second;
    try {
        if (!session.unserialize(checkpoint.core_state))
            return RestoreResult::core_rejected;
        const auto identity = session.game_state_identity();
        if (!session
                 .restore_visual_state(checkpoint.visual, identity,
                                       checkpoint.visual.header.emulation_frame)
                 .restored())
            return RestoreResult::visual_rejected;
        auto voices = checkpoint.voices;
        for (std::size_t index = 0;
             index < voices.pcm_assets.size() && index < checkpoint.shared_pcm.size(); ++index)
            voices.pcm_assets[index].samples = *checkpoint.shared_pcm[index].second;
        if (!session
                 .restore_audio_hd_detector_windows(checkpoint.audio_header, identity,
                                                    checkpoint.detector_windows)
                 .restored())
            return audio ? RestoreResult::audio_detector_rejected : RestoreResult::restored;
        if (!session.restore_audio_hd_voices(checkpoint.audio_header, identity, voices).restored())
            return audio ? RestoreResult::audio_voices_rejected : RestoreResult::restored;
        // The Engine exports the pending audio of a take in progress as incomplete
        // (contracts.md C4), and then rejects restoring it. Navigation is silent, so the
        // recovery goes on without it: only the audio after resuming can differ, and that
        // traversal is already an inspection (plan §5.6).
        if (!session
                 .restore_audio_hd_requests_pending(checkpoint.audio_header, identity,
                                                    checkpoint.requests)
                 .restored())
            pending_audio_not_restored_ = true;
        return RestoreResult::restored;
    } catch (...) {
        return RestoreResult::core_rejected;
    }
}

void CheckpointStore::corrupt_all_for_test() noexcept {
    for (auto &[frame, checkpoint] : checkpoints_) {
        (void)frame;
        std::fill(checkpoint.core_state.begin(), checkpoint.core_state.end(), std::uint8_t{0});
    }
}

void CheckpointStore::corrupt_visual_states_for_test() noexcept {
    for (auto &[frame, checkpoint] : checkpoints_) {
        (void)frame;
        checkpoint.visual.header.game_state_identity = "corrupt";
    }
}

} // namespace ayther::runtime
