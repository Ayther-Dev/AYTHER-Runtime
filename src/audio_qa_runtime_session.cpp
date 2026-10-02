#include "audio_qa_runtime_session.h"
#include "audio_qa_presentation.h"
#include "runtime_config.h"

#include "content_hash.h"
#include "control_message.h"
#include "engine_game_state_restore.h"
#include "engine_hd_initialization.h"
#include "engine_recording_replay.h"
#include "engine_replay_production_close.h"
#include "fact_batch.h"
#include "inherited_channel.h"
#include "initial_state_publication.h"
#include "observation_bridge.h"
#include "pcm_message.h"
#include "progress_watchdog.h"
#include "protocol_io.h"
#include "recording_header.h"
#include "recording_replay_preparation.h"
#include "replay_duration_limit.h"
#include "replay_execution_result.h"
#include "replay_production_close.h"
#include "replay_progress.h"
#include "replay_trace.h"

#include <ayther/ayther_session.h>
#include <ayther/engine/audio_observation_worker.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace ayther::runtime {
namespace {

namespace qa = ayther::audio_qa;

constexpr int qa_protocol_error = 65;
constexpr int qa_execution_error = 66;

class StreamingObservationWriter final {
  public:
    StreamingObservationWriter(qa::OwnedChannelHandle &channel, std::string run_id)
        : channel_(channel), run_id_(std::move(run_id)) {
        facts_.reserve(batch_capacity);
    }

    [[nodiscard]] bool consume_fact(const qa::EngineFactView &view) noexcept {
        if (!valid_)
            return false;
        auto fact = qa::copy_replay_trace_fact(run_id_, view);
        if (!fact) {
            valid_ = false;
            return false;
        }
        facts_.push_back(std::move(*fact));
        ++fact_count_;
        if (facts_.size() == batch_capacity)
            return flush(false);
        return true;
    }

    [[nodiscard]] bool consume_pcm(const qa::EnginePcmView &view) noexcept {
        if (!valid_)
            return false;
        auto chunk = qa::copy_replay_trace_pcm(run_id_, view);
        if (!chunk) {
            valid_ = false;
            return false;
        }
        const auto limit = pcm_limit_.load(std::memory_order_acquire);
        if (chunk->range.begin >= limit) {
            last_pcm_end_.store(chunk->range.end, std::memory_order_release);
            return true;
        }
        if (chunk->range.end > limit) {
            const auto frames = chunk->range.end - chunk->range.begin;
            if (frames == 0U || chunk->bytes.size() % frames != 0U) {
                valid_ = false;
                return false;
            }
            const auto bytes_per_frame = chunk->bytes.size() / frames;
            const auto retained_frames = limit - chunk->range.begin;
            chunk->bytes.resize(static_cast<std::size_t>(retained_frames) * bytes_per_frame);
            chunk->range.end = limit;
            chunk->sha256 = {qa::Availability::known, qa::pcm_sha256(chunk->bytes), {}};
        }

        const auto can_append = [&] {
            if (!pending_pcm_)
                return false;
            if (pending_pcm_->run_id != chunk->run_id ||
                pending_pcm_->capture_point != chunk->capture_point ||
                pending_pcm_->format.pcm != chunk->format.pcm ||
                pending_pcm_->format.sample_rate != chunk->format.sample_rate ||
                pending_pcm_->format.channels != chunk->format.channels ||
                pending_pcm_->range.timeline_id != chunk->range.timeline_id ||
                pending_pcm_->range.sample_rate != chunk->range.sample_rate ||
                pending_pcm_->range.end != chunk->range.begin ||
                pending_pcm_->producer_sequence >= chunk->producer_sequence ||
                pending_pcm_->bytes.size() > pcm_batch_bytes ||
                pending_pcm_->discontinuities.size() + chunk->discontinuities.size() >
                    qa::max_audio_discontinuities ||
                pending_pcm_->cause_ids.size() + chunk->cause_ids.size() > qa::max_fact_causes ||
                chunk->bytes.size() > pcm_batch_bytes - pending_pcm_->bytes.size()) {
                return false;
            }
            return true;
        }();
        if (pending_pcm_ && !can_append && !flush_pcm())
            return false;
        if (!pending_pcm_) {
            pending_pcm_ = std::move(*chunk);
        } else {
            pending_pcm_->range.end = chunk->range.end;
            pending_pcm_->bytes.insert(pending_pcm_->bytes.end(), chunk->bytes.begin(),
                                       chunk->bytes.end());
            pending_pcm_->discontinuities.insert(pending_pcm_->discontinuities.end(),
                                                 chunk->discontinuities.begin(),
                                                 chunk->discontinuities.end());
            for (const auto &cause : chunk->cause_ids)
                if (std::find(pending_pcm_->cause_ids.begin(), pending_pcm_->cause_ids.end(),
                              cause) == pending_pcm_->cause_ids.end())
                    pending_pcm_->cause_ids.push_back(cause);
        }
        last_pcm_end_.store(view.range.end, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool flush(const bool finalize_pcm) noexcept {
        if (!valid_)
            return false;
        try {
            std::size_t offset{};
            while (offset < facts_.size()) {
                std::size_t count = facts_.size() - offset;
                bool written{};
                while (count != 0U) {
                    const std::vector<qa::Fact> batch{
                        facts_.begin() + static_cast<std::ptrdiff_t>(offset),
                        facts_.begin() + static_cast<std::ptrdiff_t>(offset + count)};
                    const auto encoded = qa::encode_fact_batch(batch, sequence_);
                    if (const auto *message = std::get_if<std::vector<std::byte>>(&encoded)) {
                        if (!qa::write_channel(channel_, *message)) {
                            valid_ = false;
                            return false;
                        }
                        ++sequence_;
                        offset += count;
                        written = true;
                        break;
                    }
                    const auto *error = std::get_if<qa::FactBatchError>(&encoded);
                    if (error == nullptr || *error != qa::FactBatchError::batch_too_large ||
                        count == 1U) {
                        valid_ = false;
                        return false;
                    }
                    count /= 2U;
                }
                if (!written) {
                    valid_ = false;
                    return false;
                }
            }
            facts_.clear();
            return !finalize_pcm || flush_pcm();
        } catch (...) {
            valid_ = false;
            return false;
        }
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] std::uint64_t fact_count() const noexcept { return fact_count_; }
    [[nodiscard]] std::uint64_t pcm_count() const noexcept { return pcm_count_; }
    [[nodiscard]] std::uint64_t pcm_bytes() const noexcept { return pcm_bytes_; }
    [[nodiscard]] std::uint64_t next_sequence() const noexcept { return sequence_; }
    void set_pcm_limit(const std::uint64_t limit) noexcept {
        pcm_limit_.store(limit, std::memory_order_release);
    }
    [[nodiscard]] bool pcm_complete() const noexcept {
        return last_pcm_end_.load(std::memory_order_acquire) >=
               pcm_limit_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t last_pcm_end() const noexcept {
        return last_pcm_end_.load(std::memory_order_acquire);
    }

    static void receive_fact(void *context, std::string_view,
                             const qa::EngineFactView &fact) noexcept {
        if (context != nullptr)
            (void)static_cast<StreamingObservationWriter *>(context)->consume_fact(fact);
    }

    static void receive_pcm(void *context, std::string_view,
                            const qa::EnginePcmView &pcm) noexcept {
        if (context != nullptr)
            (void)static_cast<StreamingObservationWriter *>(context)->consume_pcm(pcm);
    }

  private:
    // Production facts in the Golden Axe campaign fit 256 records per
    // protocol payload. Flushing at that boundary avoids repeatedly building
    // rejected 1024- and 512-record candidates while the producer queues are
    // live; the encoder still halves a batch when an unusually large fact
    // requires it.
    static constexpr std::size_t batch_capacity = 256U;
    static constexpr std::size_t pcm_batch_bytes = 128U * 1024U;

    [[nodiscard]] bool flush_pcm() noexcept {
        if (!pending_pcm_)
            return true;
        pending_pcm_->sha256 = {qa::Availability::known, qa::pcm_sha256(pending_pcm_->bytes), {}};
        const auto encoded = qa::encode_pcm_message(*pending_pcm_, sequence_);
        const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
        if (message == nullptr || !qa::write_channel(channel_, *message) ||
            pending_pcm_->bytes.size() > (std::numeric_limits<std::uint64_t>::max)() - pcm_bytes_) {
            valid_ = false;
            return false;
        }
        ++sequence_;
        ++pcm_count_;
        pcm_bytes_ += pending_pcm_->bytes.size();
        pending_pcm_.reset();
        return true;
    }

    qa::OwnedChannelHandle &channel_;
    std::string run_id_;
    std::vector<qa::Fact> facts_;
    std::optional<qa::AudioChunk> pending_pcm_;
    std::uint64_t sequence_{2U};
    std::uint64_t fact_count_{};
    std::uint64_t pcm_count_{};
    std::uint64_t pcm_bytes_{};
    std::atomic<std::uint64_t> pcm_limit_{(std::numeric_limits<std::uint64_t>::max)()};
    std::atomic<std::uint64_t> last_pcm_end_{};
    bool valid_{true};
};

template <class ObservationBridge>
void drain_observations(ObservationBridge &bridge, StreamingObservationWriter &writer) noexcept {
    while (bridge.try_consume_fact(&writer, StreamingObservationWriter::receive_fact)) {
    }
    while (bridge.try_consume_pcm(&writer, StreamingObservationWriter::receive_pcm)) {
    }
}

template <class ObservationBridge> struct ObservationDrainContext {
    ObservationBridge *bridge{};
    StreamingObservationWriter *writer{};
};

template <class ObservationBridge>
[[nodiscard]] bool drain_observation_cycle(void *const value) noexcept {
    auto &context = *static_cast<ObservationDrainContext<ObservationBridge> *>(value);
    bool consumed =
        context.bridge->try_consume_fact(context.writer, StreamingObservationWriter::receive_fact);
    consumed =
        context.bridge->try_consume_pcm(context.writer, StreamingObservationWriter::receive_pcm) ||
        consumed;
    return consumed;
}

template <class ObservationBridge>
void stop_observation_worker(ayther::engine::audio_observation::ObservationConsumerWorker &worker,
                             ObservationBridge &bridge,
                             StreamingObservationWriter &writer) noexcept {
    bridge.close();
    worker.request_stop();
    worker.join();
    drain_observations(bridge, writer);
    (void)writer.flush(true);
}

[[nodiscard]] bool loss_free(const qa::ObservationBridgeLosses &losses) {
    return losses.invalid_producer == 0U && losses.invalid_fact == 0U && losses.full_fact == 0U &&
           losses.closed_fact == 0U && losses.invalid_pcm == 0U && losses.full_pcm == 0U &&
           losses.closed_pcm == 0U;
}

template <class T> qa::InitialCollection<T> empty_collection(const std::string &run_id) {
    const qa::FactId proof{run_id, "initial-state", 1};
    return {{qa::Availability::known, std::vector<T>{}, {}}, {qa::Availability::known, proof, {}}};
}

[[nodiscard]] qa::InitialState fresh_initial_state(const std::string &run_id,
                                                   const std::string &game_state_id) {
    const qa::FactId proof{run_id, "initial-state", 1};
    qa::InitialState state;
    state.run_id = run_id;
    state.game_state_id = game_state_id;
    state.game_restore_result = qa::RestoreResult::succeeded;
    state.hd_initialization = qa::HdInitialization::fresh;
    state.initialization_reason = "hd_state_absent";
    state.detector = {qa::Availability::known, qa::DetectorInitial{true, 0, 0, {}}, {}};
    state.detector_observed_by = {qa::Availability::known, proof, {}};
    state.windows = empty_collection<qa::InitialWindow>(run_id);
    state.voices = empty_collection<qa::Occurrence>(run_id);
    state.requests = empty_collection<qa::InitialRequest>(run_id);
    state.pending_audio = empty_collection<qa::PendingAudio>(run_id);
    return state;
}

[[nodiscard]] bool acknowledge_initial_state(void *, const qa::InitialState &) noexcept {
    return true;
}

void acknowledge_progress(void *, const qa::ReplayProgressEvent &) noexcept {}

[[nodiscard]] std::vector<std::byte> read_recording(const std::filesystem::path &path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U || size > qa::max_recording_bytes)
        return {};
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    std::vector<char> characters{std::istreambuf_iterator<char>{input},
                                 std::istreambuf_iterator<char>{}};
    if (input.bad() || characters.size() != size)
        return {};
    std::vector<std::byte> bytes(characters.size());
    std::memcpy(bytes.data(), characters.data(), characters.size());
    return bytes;
}

[[nodiscard]] qa::ContentIdentity state_identity(const std::vector<std::uint8_t> &state) noexcept {
    return qa::identify_content(std::as_bytes(std::span{state}));
}

[[nodiscard]] std::string digest_id(const qa::ContentIdentity &identity) {
    constexpr std::string_view digits{"0123456789abcdef"};
    std::string result{"state-"};
    result.reserve(result.size() + identity.sha256.size() * 2U);
    for (const auto byte : identity.sha256) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

[[nodiscard]] bool send_result(qa::OwnedChannelHandle &channel,
                               const qa::ReplayExecutionResult &result,
                               const std::uint64_t sequence = 2U) {
    const auto encoded = qa::encode_replay_execution_result(result, sequence);
    const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
    return message != nullptr && qa::write_channel(channel, *message);
}

} // namespace

template <class ObservationBridge>
int run_audio_qa_session_with_bridge(const RuntimeOptions &options) noexcept {
    auto control_result = qa::adopt_inherited_data_channel(options.qa_control_channel);
    auto data_result = qa::adopt_inherited_data_channel(options.qa_data_channel);
    auto *control = std::get_if<qa::OwnedChannelHandle>(&control_result);
    auto *data = std::get_if<qa::OwnedChannelHandle>(&data_result);
    if (control == nullptr || data == nullptr || options.qa_run_id.empty())
        return qa_protocol_error;

    const auto request_message = qa::read_protocol_message(*control, 1U);
    const auto *request_bytes = std::get_if<std::vector<std::byte>>(&request_message);
    if (request_bytes == nullptr)
        return qa_protocol_error;
    const auto decoded = qa::decode_request_message(*request_bytes, 1U);
    const auto *pending = std::get_if<qa::Request>(&decoded);
    if (pending == nullptr || pending->take_ids.size() != 1U)
        return qa_protocol_error;

    qa::Request accepted = *pending;
    accepted.admission = qa::Admission::accepted;
    const auto admission = qa::encode_admission_message(accepted, 1U);
    const auto *admission_bytes = std::get_if<std::vector<std::byte>>(&admission);
    if (admission_bytes == nullptr || !qa::write_channel(*data, *admission_bytes))
        return qa_protocol_error;

    qa::ReplayExecutionResult result;
    result.presentation.mode = options.qa_presentation;
    if (options.qa_presentation == "visible")
        result.presentation.code = "not_started";
    result.run_id = options.qa_run_id;
    result.take_id = pending->take_ids.front();
    result.code = "recording_unavailable";
    try {
        const auto recording = read_recording(result.take_id);
        const auto layout = qa::decode_recording_layout(recording);
        if (recording.empty() || layout.error != qa::RecordingLayoutError::none) {
            (void)send_result(*data, result);
            return qa_execution_error;
        }
        result.recording_frames = layout.layout.frame_count;

        auto bridge = std::make_unique<ObservationBridge>(result.run_id);
        StreamingObservationWriter writer{*data, result.run_id};
        ObservationDrainContext<ObservationBridge> drain_context{bridge.get(), &writer};
        ayther::engine::audio_observation::ObservationConsumerWorker observation_worker{
            &drain_context, drain_observation_cycle<ObservationBridge>};
        if (!observation_worker.start()) {
            result.code = "observation_worker_start_failed";
            (void)send_result(*data, result);
            return qa_execution_error;
        }
        std::unique_ptr<AytherSession> session;
        std::unique_ptr<AudioQaPresentation> presentation;
        bool observation_stopped{};
        qa::ObservationBridgeLosses observation_losses;
        const auto stop_capture = [&] {
            if (observation_stopped)
                return;
            presentation.reset();
            session.reset();
            stop_observation_worker(observation_worker, *bridge, writer);
            observation_stopped = true;
            observation_losses = bridge->losses();
            result.trace.observed_fact_count = writer.fact_count();
            result.trace.loss_free = writer.valid() && loss_free(observation_losses);
        };
        const auto fail_session = [&](const std::string_view code) {
            stop_capture();
            result.code = code;
            return send_result(*data, result, writer.next_sequence()) ? qa_execution_error
                                                                      : qa_protocol_error;
        };
        AytherSession::Config config;
        config.core_path = options.core_path;
        config.rom_path = options.rom_path;
        config.pack_path = options.pack_path;
        config.trust_registry = options.trust_registry_path;
        config.enable_audio = true;
        config.derive_core_pack = false;
        const auto replay_observer = bridge->observer();
        config.audio_observer = replay_observer;
        config.audio_observer.on_pcm = nullptr;
        config.core_options = options.core_options;
        config.patch_path = options.patch_path;
        auto created = AytherSession::create(config);
        if (!created)
            return fail_session("engine_session_create_failed");
        session = std::move(*created);
        const auto duration =
            qa::validate_replay_duration(layout.layout.frame_count, session->timing_fps());
        if (duration.error != qa::ReplayDurationError::none)
            return fail_session(qa::replay_duration_error_code(duration.error));
        const auto frame_period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>{1.0 / session->timing_fps()});
        const auto prepared = qa::prepare_recording_replay(recording, layout.layout, session.get(),
                                                           qa::restore_engine_game_state);
        if (!prepared.inputs || !prepared.restore.succeeded)
            return fail_session("game_state_restore_failed");

        std::vector<std::uint8_t> initial_state;
        if (!session->serialize(initial_state))
            return fail_session("initial_state_serialize_failed");
        result.initial_game_state = state_identity(initial_state);
        const auto hd = qa::initialize_engine_hd_audio(
            session.get(), digest_id(result.initial_game_state), nullptr);
        if (hd.initialization != qa::HdInitialization::fresh || hd.evidence_incomplete)
            return fail_session("fresh_hd_initialization_failed");
        if (!options.pack_path.empty()) {
            session->load_audio_events_from_pack();
            result.assignment_count = session->audio_event_assignment_count();
            if (result.assignment_count == 0U)
                return fail_session("audio_assignment_catalog_empty");
        }
        ayther::PlayerConfig player_config;
        if (options.qa_presentation == "visible") {
            result.presentation.mode = "visible";
            const auto pack = session->pack();
            auto loaded = ayther::player_config_load_checked(
                ayther::player_config_path(RuntimePaths::discover().configuration_directory(),
                                           session->game_id(), pack ? pack.info().name : ""));
            if (loaded.status != ayther::PlayerConfigLoadStatus::loaded &&
                loaded.status != ayther::PlayerConfigLoadStatus::missing)
                return fail_session("player_configuration_invalid");
            player_config = std::move(loaded.config);
            if (!player_config.profile.empty() && !session->set_profile(player_config.profile))
                return fail_session("player_profile_unavailable");
            if (player_config.have_subsystems) {
                std::uint32_t available{};
                for (std::uint32_t index{}; index < ayther::kSubsystemCount; ++index)
                    if (session->subsystem_availability(static_cast<ayther::Subsystem>(index)) !=
                        ayther::SubsystemAvailability::Absent)
                        available |= std::uint32_t{1} << index;
                session->set_subsystems_enabled_mask(player_config.subsystems & available);
            }
            for (std::uint32_t index{}; index < ayther::kAudioBusCount; ++index) {
                session->set_bus_volume(static_cast<ayther::AudioBus>(index),
                                        player_config.bus_gain[index]);
                session->set_bus_muted(static_cast<ayther::AudioBus>(index),
                                       player_config.bus_muted[index]);
            }
            presentation = std::make_unique<AudioQaPresentation>(result.presentation);
            presentation->initialize(*session, options, player_config);
        }
        if (!options.profile.empty() && !session->set_profile(options.profile))
            return fail_session("audio_profile_unavailable");
        if (options.subsystems)
            session->set_subsystems_enabled_mask(*options.subsystems);
        if (options.mute_buses)
            for (std::uint32_t index{}; index < ayther::kAudioBusCount; ++index)
                session->set_bus_muted(static_cast<ayther::AudioBus>(index),
                                       (*options.mute_buses & (std::uint32_t{1} << index)) != 0U);
        session->set_audio_runtime_substitution(true);
        if (!session->set_audio_pcm_observer(replay_observer.on_pcm)) {
            if (!presentation)
                return fail_session("pcm_observation_start_failed");
            result.presentation.code = "audible_output_unavailable";
        }

        qa::InitialStatePublication publication{
            fresh_initial_state(result.run_id, digest_id(result.initial_game_state))};
        if (!publication.publish(nullptr, acknowledge_initial_state))
            return fail_session("initial_state_publication_failed");
        auto inputs = *prepared.inputs;
        qa::ReplayProgressPublisher progress{layout.layout.frame_count, nullptr,
                                             acknowledge_progress};
        if (!progress.publish_acceptance())
            return fail_session("replay_progress_failed");
        qa::RecordingReplayLoop loop{publication, inputs, &progress};
        struct PresentedStep {
            AytherSession *session;
            AudioQaPresentation *presentation;
            std::uint32_t recording_frame{};
        } step{session.get(), presentation.get()};
        const qa::ReplayFrameOperations operations{
            &step,
            [](void *context, const std::uint32_t frame, const std::uint16_t buttons) noexcept {
                auto &value = *static_cast<PresentedStep *>(context);
                value.recording_frame = frame;
                const auto engine = qa::engine_recording_replay_operations(value.session);
                return engine.set_input(engine.context, frame, buttons);
            },
            [](void *context) noexcept -> qa::ReplayFrameOperationResult {
                auto &value = *static_cast<PresentedStep *>(context);
                try {
                    const auto &view = value.session->step();
                    if (value.presentation)
                        value.presentation->present(*value.session, view, value.recording_frame);
                    return {true, view.frame_index, "ok", {}};
                } catch (...) {
                    return {false, 0U, "replay_step_failed", {}};
                }
            }};
        auto next_frame = std::chrono::steady_clock::now();
        bool cancelled{};
        for (std::uint32_t frame{}; frame < layout.layout.frame_count; ++frame) {
            if (presentation && !presentation->poll()) {
                cancelled = true;
                break;
            }
            const auto executed = loop.execute_next(operations);
            if (executed.error != qa::ReplayFrameError::none) {
                result.inputs_consumed = loop.frames_completed();
                return fail_session("recording_replay_failed");
            }
            next_frame += frame_period;
            const auto now = std::chrono::steady_clock::now();
            if (presentation && now > next_frame + frame_period) {
                result.presentation.affect(frame);
                if (result.presentation.code == "presented")
                    result.presentation.code = "cadence_degraded";
                next_frame = now;
            }
            std::this_thread::sleep_until(next_frame);
        }
        if (!cancelled) {
            const auto exhausted = loop.execute_next(operations);
            if (exhausted.error != qa::ReplayFrameError::input_exhausted) {
                result.inputs_consumed = loop.frames_completed();
                return fail_session("recording_input_boundary_failed");
            }
        }
        result.inputs_consumed = loop.frames_completed();
        const auto close_operations = qa::engine_replay_production_close_operations(session.get());
        auto closed = qa::close_replay_production(progress, close_operations, cancelled);
        if (closed.error != qa::ReplayProductionCloseError::none)
            return fail_session("audio_production_close_failed");
        std::vector<std::uint8_t> final_state;
        if (!session->serialize(final_state))
            return fail_session("final_state_serialize_failed");
        result.final_game_state = state_identity(final_state);

        // The frozen Engine boundary is expressed on the pre-DRC main input
        // timeline. The canonical capture uses the postmix output timeline, so
        // their numeric sample positions legitimately diverge when SDL applies
        // the dynamic frequency ratio. Repeated drain observations wait until
        // every frozen stream has reached the logical device; Engine then
        // pauses that device and reports the exact postmix output boundary.
        const auto close_started = std::chrono::steady_clock::now();
        qa::ProgressWatchdog close_watchdog{qa::WatchdogPhase::closing, 0U};
        const auto initial_output_end = writer.last_pcm_end();
        (void)close_watchdog.observe_bytes_received(initial_output_end, 0U);
        (void)close_watchdog.observe_bytes_durable(initial_output_end, 0U);
        while (!closed.drain.output_complete) {
            if (presentation)
                (void)presentation->poll(false);
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            closed.drain = close_operations.drain(close_operations.context);
            if (!closed.drain.succeeded || !closed.drain.accepted || !closed.drain.complete ||
                closed.drain.remaining_main_frames != 0U ||
                closed.drain.main_sample_limit != closed.limit.main_sample_limit) {
                return fail_session("audio_production_close_failed");
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - close_started)
                                     .count();
            const auto now_ms = static_cast<std::uint64_t>(elapsed);
            const auto current_output_end = writer.last_pcm_end();
            if (!close_watchdog.observe_bytes_received(current_output_end, now_ms) ||
                !close_watchdog.observe_bytes_durable(current_output_end, now_ms) ||
                close_watchdog.check(now_ms).has_value()) {
                return fail_session("pcm_capture_close_timeout");
            }
        }
        stop_capture();
        writer.set_pcm_limit(closed.drain.output_sample_limit);
        if (!writer.pcm_complete())
            return fail_session("pcm_capture_close_timeout");
        if (!result.trace.loss_free || writer.pcm_count() == 0U) {
            if (!writer.valid())
                result.code = "audio_trace_incomplete_writer";
            else if (observation_losses.invalid_producer != 0U)
                result.code = "audio_trace_incomplete_invalid_producer";
            else if (observation_losses.invalid_fact != 0U)
                result.code = "audio_trace_incomplete_invalid_fact";
            else if (observation_losses.full_fact != 0U) {
                result.code = "audio_trace_incomplete_full_fact";
                const auto producer = bridge->first_full_fact_producer();
                if (producer != 0U)
                    result.code += "_p" + std::to_string(producer);
            } else if (observation_losses.closed_fact != 0U)
                result.code = "audio_trace_incomplete_closed_fact";
            else if (observation_losses.invalid_pcm != 0U)
                result.code = "audio_trace_incomplete_invalid_pcm";
            else if (observation_losses.full_pcm != 0U)
                result.code = "audio_trace_incomplete_full_pcm";
            else if (observation_losses.closed_pcm != 0U)
                result.code = "audio_trace_incomplete_closed_pcm";
            else
                result.code = "audio_trace_incomplete_empty_pcm";
            return send_result(*data, result, writer.next_sequence()) ? qa_execution_error
                                                                      : qa_protocol_error;
        }
        result.succeeded = !cancelled && result.presentation.complete();
        result.code =
            cancelled ? "replay_cancelled"
                      : (result.succeeded ? "replay_evidence_streamed" : "presentation_incomplete");
        return send_result(*data, result, writer.next_sequence())
                   ? (result.succeeded ? 0 : qa_execution_error)
                   : qa_protocol_error;
    } catch (...) {
        result.code = "runtime_qa_unhandled_error";
        (void)send_result(*data, result);
        return qa_execution_error;
    }
}

int run_audio_qa_session(const RuntimeOptions &options) noexcept {
    if (options.qa_presentation == "visible")
        return run_audio_qa_session_with_bridge<qa::VisibleObservationBridge>(options);
    return run_audio_qa_session_with_bridge<qa::ProductionObservationBridge>(options);
}

} // namespace ayther::runtime
