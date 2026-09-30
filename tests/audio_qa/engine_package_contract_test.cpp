#include <ayther/engine/audio_observer.hpp>
#include <ayther/engine/audio_production_limit.hpp>
#include <ayther/engine/capabilities.hpp>

#include <cstdint>

int main() {
    using ayther::engine::audio_observation::AudioFrozenDrainResult;
    using ayther::engine::audio_observation::AudioProductionLimit;
    using ayther::engine::audio_observation::AudioVoiceFinalizationResult;
    using ayther::engine::audio_observation::contract_version;

    static_assert(contract_version.major == 1);
    static_assert(contract_version.minor == 0);
    static_assert(sizeof(AudioProductionLimit::main_sample_limit) == sizeof(std::uint64_t));
    static_assert(sizeof(AudioVoiceFinalizationResult::finalized_voices) == sizeof(std::uint64_t));
    static_assert(sizeof(AudioFrozenDrainResult::drained_main_frames) == sizeof(std::uint64_t));

    const auto linked_engine = ayther::engine::version();
    return linked_engine.major == 0xffffu ? 1 : 0;
}
