#pragma once

#include "audio_chunk.h"
#include "contract_version.h"
#include "reference_model.h"

namespace ayther::audio_qa {

struct StateImage {
    std::string image_id;
    ContractVersion version{hd_state_schema_version};
    ContentIdentity content;
};

template <class T> struct InitialCollection {
    Field<std::vector<T>> entries;
    Field<FactId> observed_by;
};

struct DetectorInitial {
    bool reset_verified{};
    std::uint64_t active_events{};
    std::uint64_t pending_inputs{};
    // Detector history, learned identities and previous active signatures are Engine-owned.
    Field<StateImage> continuation_state;
};

struct InitialWindow {
    std::uint64_t key{};
    std::uint64_t start_frame{};
    std::uint64_t end_frame{};
    std::uint64_t cut_frame{};
    std::uint32_t channel_mask{};
    bool looping{};
};

struct InitialRequest {
    std::string request_id;
    std::uint64_t business_key{};
    std::string track_id;
    Field<std::uint64_t> scheduled_frame;
};

struct PendingAudio {
    std::string queue_id;
    AudioFormat format;
    SampleFrameRange range;
    ContentIdentity pcm_content;
    // Includes resampler/filter phase and buffered support, not only queued PCM.
    Field<StateImage> continuation_state;
};

enum class HdInitialization { unknown, fresh, restored };
enum class RestoreResult { not_attempted, succeeded, failed };

struct InitialState {
    std::string run_id;
    std::string game_state_id;
    RestoreResult game_restore_result{RestoreResult::not_attempted};
    ContractVersion hd_version{hd_state_schema_version};
    Field<ContractVersion> supplied_hd_version;
    HdInitialization hd_initialization{HdInitialization::unknown};
    RestoreResult hd_restore_result{RestoreResult::not_attempted};
    std::string initialization_reason;
    Field<StateImage> engine_state_image;
    Field<DetectorInitial> detector;
    Field<FactId> detector_observed_by;
    InitialCollection<InitialWindow> windows;
    InitialCollection<Occurrence> voices;
    InitialCollection<InitialRequest> requests;
    InitialCollection<PendingAudio> pending_audio;
};

[[nodiscard]] bool well_formed(const InitialState &state) noexcept;
// Completeness of the description only; import, hash and actual reset verification are separate.
[[nodiscard]] bool has_complete_hd_description(const InitialState &state) noexcept;

} // namespace ayther::audio_qa
