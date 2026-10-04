#include "audio_qa_runtime_session.h"
#include "audio_qa_inspection.h"
#include "audio_qa_presentation.h"
#include "runtime_config.h"

#include "cancellation_message.h"
#include "content_hash.h"
#include "control_message.h"
#include "engine_game_state_restore.h"
#include "engine_hd_initialization.h"
#include "engine_recording_replay.h"
#include "engine_replay_production_close.h"
#include "fact_batch.h"
#include "inherited_channel.h"
#include "initial_state_publication.h"
#include "inspection_fact_builder.h"
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
#include "session_status_message.h"

#include "debug_view.h"
#include "frame_record.h"
#include "input_script.h"
#include "inspection_state.h"
#include "key_router.h"
#include "presentation_health.h"
#include "recovery_planner.h"
#include "render_observation.h"
#include "replay_messages.h"
#include "take_clock.h"

#include <ayther/ayther_session.h>
#include <ayther/engine/audio_observation_worker.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
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
        const std::lock_guard lock{mutex_};
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
            return flush_unlocked(false);
        return true;
    }

    // Spec 002 (contracts.md C2): a fact the Runtime builds itself (inspection events).
    [[nodiscard]] bool push_fact(qa::Fact fact) noexcept {
        const std::lock_guard lock{mutex_};
        if (!valid_)
            return false;
        try {
            facts_.push_back(std::move(fact));
        } catch (...) {
            valid_ = false;
            return false;
        }
        ++fact_count_;
        if (facts_.size() == batch_capacity)
            return flush_unlocked(false);
        return true;
    }

    // Spec 002 (contracts.md C1-3, C1-6): a `session_status` takes the next sequence of the
    // data channel. It is never evidence: a failure to send it does not invalidate evidence.
    template <class Encode> [[nodiscard]] bool send_status(Encode encode) noexcept {
        const std::lock_guard lock{mutex_};
        if (!valid_)
            return false;
        try {
            const auto encoded = encode(sequence_);
            const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
            if (message == nullptr || !qa::write_channel(channel_, *message))
                return false;
            ++sequence_;
            return true;
        } catch (...) {
            return false;
        }
    }

    // The terminal takes its sequence like any other message, so that the live state of a
    // window kept paused after it (RF-2.8) continues the sequence.
    [[nodiscard]] bool send_terminal(const qa::ReplayExecutionResult &result) noexcept {
        const std::lock_guard lock{mutex_};
        try {
            const auto encoded = qa::encode_replay_execution_result(result, sequence_);
            const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
            if (message == nullptr || !qa::write_channel(channel_, *message))
                return false;
            ++sequence_;
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] bool consume_pcm(const qa::EnginePcmView &view) noexcept {
        const std::lock_guard lock{mutex_};
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
        const std::lock_guard lock{mutex_};
        return flush_unlocked(finalize_pcm);
    }

    [[nodiscard]] bool flush_unlocked(const bool finalize_pcm) noexcept {
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
    [[nodiscard]] std::uint64_t next_sequence() noexcept {
        const std::lock_guard lock{mutex_};
        return sequence_;
    }
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

    std::mutex mutex_;
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

// Spec 002 (contracts.md C1-5): every terminal says separately how the playback ended,
// the traversal and why the evidence is incomplete.
[[nodiscard]] bool send_result(qa::OwnedChannelHandle &channel, qa::ReplayExecutionResult result,
                               const std::uint64_t sequence = 2U, const bool cancelled = false) {
    qa::describe_linear_terminal(result, cancelled);
    const auto encoded = qa::encode_replay_execution_result(result, sequence);
    const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
    return message != nullptr && qa::write_channel(channel, *message);
}

} // namespace

// Reads the control channel without blocking: true once the supervisor asked to cancel
// this run or closed the channel before the terminal.
[[nodiscard]] bool supervisor_cancelled(qa::OwnedChannelHandle &control,
                                        const std::string &request_id,
                                        const std::string &run_id) noexcept {
    switch (qa::poll_channel(control)) {
    case qa::ChannelPollResult::idle:
    case qa::ChannelPollResult::failed:
        return false;
    case qa::ChannelPollResult::closed:
        return true;
    case qa::ChannelPollResult::pending:
        break;
    }
    const auto message = qa::read_protocol_message(control, 1U);
    const auto *bytes = std::get_if<std::vector<std::byte>>(&message);
    if (bytes == nullptr)
        return true;
    const auto decoded =
        qa::decode_cancellation_message(*bytes, qa::CancellationStage::requested, 2U);
    const auto *cancel = std::get_if<qa::CancellationMessage>(&decoded);
    return cancel != nullptr && cancel->request_id == request_id && cancel->run_id == run_id;
}

namespace {

namespace ri = ayther::replay_inspection;

// Spec 002 (plan §5.6, §8 P-4): any recovery longer than this is a failure (RF-5.6).
constexpr std::chrono::milliseconds recovery_limit{5000};
// Spec 002 (plan §5.7, §8 P-9): the drain of a pause waits at most one second.
constexpr std::chrono::milliseconds pause_drain_limit{1000};
// Spec 002 (plan §8 P-12): a checkpoint every K = 60 frames, 512 MiB per take.
constexpr std::uint32_t checkpoint_interval = 60U;
// C1-3: at most one live state per presented frame; the Runtime sends fewer.
constexpr std::chrono::milliseconds playing_state_period{250};

[[nodiscard]] std::string_view phase_code(const ri::InspectionPhase phase,
                                          const bool ended) noexcept {
    switch (phase) {
    case ri::InspectionPhase::preparing:
        return "preparing";
    case ri::InspectionPhase::playing:
        return "playing";
    case ri::InspectionPhase::pausing:
        return "pausing";
    case ri::InspectionPhase::paused:
        return ended ? "ended_paused" : "paused";
    case ri::InspectionPhase::recovering:
        return "recovering";
    case ri::InspectionPhase::interrupted:
        return "interrupted";
    case ri::InspectionPhase::closing:
    case ri::InspectionPhase::failed:
        return "closing";
    }
    return "closing";
}

// Spec 002, contracts.md C3 (RF-7): keeps a copy of the render observation of the last
// published frame; the views only live during the callback.
class LastRenderObservation final : public ayther::engine::render_observation::RenderObserver {
  public:
    void on_render_frame(
        const ayther::engine::render_observation::RenderFrameView &frame) noexcept override {
        try {
            observation = ri::render::copy_observation(frame);
            valid = true;
        } catch (...) {
            valid = false;
        }
    }
    ri::render::RenderObservation observation;
    bool valid{};
};

[[nodiscard]] std::string not_composable_reason(const ri::render::Composability value) {
    switch (value) {
    case ri::render::Composability::composable:
        return {};
    case ri::render::Composability::raster_split:
        return "raster_split";
    case ri::render::Composability::fade:
        return "fade";
    case ri::render::Composability::line_hscroll:
        return "line_hscroll";
    case ri::render::Composability::column_vscroll:
        return "column_vscroll";
    case ri::render::Composability::other:
        break;
    }
    return "other";
}

// Spec 002, BR-156 (plan §8): the time marks of the router and of the presentation for the
// measurements of the inspection, written only when AYTHER_QA_TIMING_LOG names a file.
class TimingLog final {
  public:
    explicit TimingLog(const std::string &path) {
        if (!path.empty())
            output_.open(std::filesystem::path{path}, std::ios::app);
    }
    template <class... Values> void mark(const Values &...values) {
        if (!output_)
            return;
        output_ << std::fixed << std::setprecision(3);
        bool first = true;
        ((output_ << (first ? "" : ",") << values, first = false), ...);
        output_ << '\n';
        output_.flush();
    }

  private:
    std::ofstream output_;
};

// The scripted input of the QA tests (AYTHER_QA_INPUT_SCRIPT), read only in a QA session.
enum class ScriptLoad { absent, loaded, invalid };

[[nodiscard]] std::string environment_value(const char *name) {
#ifdef _WIN32
    char *raw{};
    std::size_t size{};
    if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr)
        return {};
    const std::unique_ptr<char, decltype(&std::free)> value{raw, &std::free};
    return std::string{value.get()};
#else
    const char *value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string{value};
#endif
}

[[nodiscard]] ScriptLoad load_input_script(std::optional<ri::InputScript> &script) {
    const auto path = environment_value("AYTHER_QA_INPUT_SCRIPT");
    if (path.empty())
        return ScriptLoad::absent;
    std::ifstream input(std::filesystem::path{path}, std::ios::binary);
    if (!input)
        return ScriptLoad::invalid;
    const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    auto parsed = ri::parse_input_script(text);
    auto *steps = std::get_if<std::vector<ri::ScriptStep>>(&parsed);
    if (steps == nullptr)
        return ScriptLoad::invalid;
    script.emplace(std::move(*steps));
    return ScriptLoad::loaded;
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
    // Spec 002 (contracts.md C1-2): a request with its language negotiated protocol 1.1.
    const bool protocol_v11 = pending->language.has_value();

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
        const auto frames = layout.layout.frame_count;

        auto bridge = std::make_unique<ObservationBridge>(result.run_id);
        StreamingObservationWriter writer{*data, result.run_id};
        // DI-8: facts and PCM of silent production never reach the linear evidence.
        SilentProductionGate gate{bridge->observer()};
        // BR-156 (P-9): with a timing log, the audio device threads also mark when the output
        // reaches the device, unless AYTHER_QA_AUDIO_TAP=0 asks for a run without them. It
        // outlives the session, which is declared after it.
        std::unique_ptr<AudioTimingTap> audio_tap;
        if (!environment_value("AYTHER_QA_TIMING_LOG").empty() &&
            environment_value("AYTHER_QA_AUDIO_TAP") != "0") {
            audio_tap = std::make_unique<AudioTimingTap>();
            gate.set_timing_tap(audio_tap.get());
        }

        // Spec 002 (contracts.md C1-3): the live state, only with protocol 1.1.
        // Plan §5.11: only the last take with an open window stays paused at its end; the
        // controller learns it once the window opened.
        ri::InspectionController controller{frames, false};
        bool confirmed_linear{};
        // RF-2.9: a post-end inspection is a run of its own, `<run>-inspection-<n>`.
        std::string current_run_id = result.run_id;
        std::uint32_t post_end_runs{};
        std::optional<std::string> last_state_phase;
        std::optional<std::uint32_t> last_state_frame;
        auto last_state_sent = std::chrono::steady_clock::now();
        bool debug_visible{};
        const auto report_state = [&](const bool force) {
            if (!protocol_v11)
                return;
            const auto phase = std::string{phase_code(
                controller.phase(), confirmed_linear && controller.position() == frames - 1U)};
            const auto frame = controller.position();
            const auto now = std::chrono::steady_clock::now();
            const bool changed =
                phase != last_state_phase || (phase != "playing" && frame != last_state_frame);
            if (!force && !changed &&
                (phase != "playing" || now - last_state_sent < playing_state_period))
                return;
            qa::ReplayStateMessage state{
                current_run_id,
                options.qa_take_position,
                phase,
                frame ? std::optional<std::uint64_t>{*frame} : std::optional<std::uint64_t>{},
                frames,
                debug_visible,
                phase == "interrupted" ? controller.interruption_cause() : std::string{}};
            if (phase == "interrupted" && state.interruption_cause.empty())
                state.interruption_cause = "unknown";
            (void)writer.send_status([&state](const std::uint64_t sequence) {
                return qa::encode_replay_state(state, sequence);
            });
            last_state_phase = phase;
            last_state_frame = frame;
            last_state_sent = now;
        };
        report_state(true);

        ObservationDrainContext<ObservationBridge> drain_context{bridge.get(), &writer};
        ayther::engine::audio_observation::ObservationConsumerWorker observation_worker{
            &drain_context, drain_observation_cycle<ObservationBridge>};
        if (!observation_worker.start()) {
            result.code = "observation_worker_start_failed";
            (void)send_result(*data, result, writer.next_sequence());
            return qa_execution_error;
        }
        // Declared before the session: it must outlive it (contracts.md C3).
        LastRenderObservation render_observation;
        std::unique_ptr<AytherSession> session;
        std::unique_ptr<AudioQaPresentation> presentation;
        bool observation_stopped{};
        qa::ObservationBridgeLosses observation_losses;
        const auto stop_observation = [&] {
            if (observation_stopped)
                return;
            stop_observation_worker(observation_worker, *bridge, writer);
            observation_stopped = true;
            observation_losses = bridge->losses();
            result.trace.observed_fact_count = writer.fact_count();
            result.trace.loss_free = writer.valid() && loss_free(observation_losses);
        };
        const auto stop_capture = [&] {
            if (observation_stopped)
                return;
            presentation.reset();
            session.reset();
            stop_observation();
        };
        const auto fail_session = [&](const std::string_view code) {
            stop_capture();
            result.code = code;
            return send_result(*data, result, writer.next_sequence()) ? qa_execution_error
                                                                      : qa_protocol_error;
        };
        std::optional<ri::InputScript> script;
        if (load_input_script(script) == ScriptLoad::invalid)
            return fail_session("input_script_invalid");

        AytherSession::Config config;
        config.core_path = options.core_path;
        config.rom_path = options.rom_path;
        config.pack_path = options.pack_path;
        config.trust_registry = options.trust_registry_path;
        config.enable_audio = true;
        config.derive_core_pack = false;
        config.audio_observer = gate.observer();
        config.render_observer = &render_observation;
        config.audio_observer.on_pcm = nullptr;
        config.core_options = options.core_options;
        config.patch_path = options.patch_path;
        auto created = AytherSession::create(config);
        if (!created)
            return fail_session("engine_session_create_failed");
        session = std::move(*created);
        const auto duration = qa::validate_replay_duration(frames, session->timing_fps());
        if (duration.error != qa::ReplayDurationError::none)
            return fail_session(qa::replay_duration_error_code(duration.error));
        const ri::TakeClock clock{frames, session->timing_fps()};
        const auto frame_period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>{1.0 / session->timing_fps()});
        const auto prepared = qa::prepare_recording_replay(recording, layout.layout, session.get(),
                                                           qa::restore_engine_game_state);
        if (!prepared.inputs || !prepared.restore.succeeded)
            return fail_session("game_state_restore_failed");
        // RF-5.2: navigation re-simulates with the recorded input of each frame.
        std::vector<std::uint16_t> recorded_buttons;
        recorded_buttons.reserve(frames);
        {
            auto scan = *prepared.inputs;
            while (const auto input = scan.next())
                recorded_buttons.push_back(input->buttons);
        }
        if (recorded_buttons.size() != frames)
            return fail_session("recording_input_boundary_failed");

        std::vector<std::uint8_t> initial_state;
        if (!session->serialize(initial_state))
            return fail_session("initial_state_serialize_failed");
        result.initial_game_state = state_identity(initial_state);
        const auto hd = qa::initialize_engine_hd_audio(
            session.get(), digest_id(result.initial_game_state), nullptr);
        if (hd.initialization != qa::HdInitialization::fresh || hd.evidence_incomplete)
            return fail_session("fresh_hd_initialization_failed");
        // Spec 002 (RF-2.1, RF-2.2): a selected pack is loaded or the take fails, never
        // replayed as the original game. A valid pack without audio catalog replays and
        // records its number of assignments, zero included.
        if (!options.pack_path.empty()) {
            if (!session->has_pack())
                return fail_session("pack_open_failed");
            session->load_audio_events_from_pack();
            result.assignment_count = session->audio_event_assignment_count();
        }
        ayther::PlayerConfig player_config;
        std::optional<std::uint32_t> subsystem_mask;
        std::string active_profile;
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
            active_profile = player_config.profile;
            if (player_config.have_subsystems) {
                std::uint32_t available{};
                for (std::uint32_t index{}; index < ayther::kSubsystemCount; ++index)
                    if (session->subsystem_availability(static_cast<ayther::Subsystem>(index)) !=
                        ayther::SubsystemAvailability::Absent)
                        available |= std::uint32_t{1} << index;
                subsystem_mask = player_config.subsystems & available;
                session->set_subsystems_enabled_mask(*subsystem_mask);
            }
            for (std::uint32_t index{}; index < ayther::kAudioBusCount; ++index) {
                session->set_bus_volume(static_cast<ayther::AudioBus>(index),
                                        player_config.bus_gain[index]);
                session->set_bus_muted(static_cast<ayther::AudioBus>(index),
                                       player_config.bus_muted[index]);
            }
            presentation = std::make_unique<AudioQaPresentation>(result.presentation);
            presentation->initialize(*session, options, player_config);
            controller =
                ri::InspectionController{frames, presentation->ready() && options.qa_last_take};
        }
        if (!options.profile.empty() && !session->set_profile(options.profile))
            return fail_session("audio_profile_unavailable");
        if (!options.profile.empty())
            active_profile = options.profile;
        if (options.subsystems) {
            subsystem_mask = *options.subsystems;
            session->set_subsystems_enabled_mask(*options.subsystems);
        }
        if (options.mute_buses)
            for (std::uint32_t index{}; index < ayther::kAudioBusCount; ++index)
                session->set_bus_muted(static_cast<ayther::AudioBus>(index),
                                       (*options.mute_buses & (std::uint32_t{1} << index)) != 0U);
        session->set_audio_runtime_substitution(true);
        if (!session->set_audio_pcm_observer(gate.pcm_callback())) {
            if (!presentation)
                return fail_session("pcm_observation_start_failed");
            result.presentation.code = "audible_output_unavailable";
        }

        qa::InitialStatePublication publication{
            fresh_initial_state(result.run_id, digest_id(result.initial_game_state))};
        if (!publication.publish(nullptr, acknowledge_initial_state))
            return fail_session("initial_state_publication_failed");
        auto inputs = *prepared.inputs;
        qa::ReplayProgressPublisher progress{frames, nullptr, acknowledge_progress};
        if (!progress.publish_acceptance())
            return fail_session("replay_progress_failed");
        qa::RecordingReplayLoop loop{publication, inputs, &progress};

        // Spec 002 (plan §5.6): checkpoints only when the take can be inspected.
        const bool inspectable = presentation != nullptr || script.has_value();
        CheckpointStore checkpoints{checkpoint_interval, ri::checkpoint_budget_bytes};
        if (inspectable && !checkpoints.capture(*session, ri::initial_checkpoint_frame))
            return fail_session("checkpoint_capture_failed");

        // One frame of the take: the next linear input, or a recorded one again after
        // navigation. The view stays valid until the next step.
        struct Stepper {
            AytherSession *session;
            const FrameView *view{};
        } stepper{session.get()};
        const qa::ReplayFrameOperations operations{
            &stepper,
            [](void *context, const std::uint32_t, const std::uint16_t buttons) noexcept {
                static_cast<Stepper *>(context)->session->set_input(0, buttons);
                return qa::ReplayFrameOperationResult{true, 0U, "ok", {}};
            },
            [](void *context) noexcept -> qa::ReplayFrameOperationResult {
                auto &value = *static_cast<Stepper *>(context);
                try {
                    value.view = &value.session->step();
                    return {true, value.view->frame_index, "ok", {}};
                } catch (...) {
                    return {false, 0U, "replay_step_failed", {}};
                }
            }};
        std::optional<std::uint64_t> engine_base;
        std::optional<std::uint32_t> session_frame;
        bool audible = true;
        const auto set_audible = [&](const bool value) {
            if (audible == value)
                return;
            audible = value;
            gate.set_silent(!value);
            session->set_audio_output_mode(value ? AudioOutputMode::audible
                                                 : AudioOutputMode::silent);
        };
        // Produces take frame `frame` from the session state of frame − 1.
        double input_started_ms{};
        double produced_ms{};
        const auto clock_ms = [&] {
            return std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        };
        const auto produce = [&](const std::uint32_t frame) -> bool {
            input_started_ms = clock_ms();
            if (frame == loop.frames_completed()) {
                const auto executed = loop.execute_next(operations);
                if (executed.error != qa::ReplayFrameError::none)
                    return false;
            } else if (frame < loop.frames_completed() && frame < recorded_buttons.size()) {
                session->set_input(0, recorded_buttons[frame]);
                try {
                    stepper.view = &session->step();
                } catch (...) {
                    return false;
                }
            } else {
                return false;
            }
            // Plan §5.8: the Engine increments its index before producing.
            if (!engine_base)
                engine_base = stepper.view->frame_index - frame - 1U;
            if (stepper.view->frame_index != *engine_base + frame + 1U)
                return false;
            produced_ms = clock_ms();
            session_frame = frame;
            return true;
        };

        ri::KeyRouter router;
        ri::PresentationHealth health;
        TimingLog timing{environment_value("AYTHER_QA_TIMING_LOG")};
        if (presentation)
            timing.mark("device", presentation->device_name(), presentation->refresh_hz());
        std::optional<double> resume_key_ms;
        std::map<std::uint32_t, std::uint64_t> visits;
        std::uint64_t event_sequence{};
        // The Runtime's own facts (C2) share the producer `inspection` and its sequence.
        std::uint64_t inspection_sequence{};
        ri::FrameMeasurer measurer;
        bool after_resume = true;
        // RF-6, RF-7, RNF-7: the record drawn by the overlay and the notice of the inspection.
        const auto replay_language = ri::parse_replay_language(pending->language.value_or("es"));
        std::optional<ri::FrameRecord> current_record;
        std::string notice_text;
        const auto push_debug = [&] {
            if (!presentation)
                return;
            const bool visible = controller.debug_visible();
            presentation->set_debug(visible,
                                    visible && current_record
                                        ? ri::debug_lines(*current_record, clock, replay_language)
                                        : std::vector<ri::DebugLine>{},
                                    notice_text);
        };
        const auto notice_of = [&](const ri::Notice notice) -> std::string {
            switch (notice) {
            case ri::Notice::none:
                return {};
            case ri::Notice::not_available:
                return std::string{
                    ri::replay_message(replay_language, ri::ReplayMessage::not_available)};
            case ri::Notice::busy:
                return std::string{ri::replay_message(replay_language, ri::ReplayMessage::busy)};
            case ri::Notice::recovery_failed:
                return std::string{
                    ri::replay_message(replay_language, ri::ReplayMessage::recovery_failed)};
            case ri::Notice::interrupted:
                return std::string{ri::replay_message(
                    replay_language,
                    ri::interruption_cause_message(controller.interruption_cause()))};
            case ri::Notice::no_next_frame:
                return std::string{
                    ri::replay_message(replay_language, ri::ReplayMessage::no_next_frame)};
            }
            return {};
        };
        std::uint64_t user_pause_ms{};
        std::optional<std::chrono::steady_clock::time_point> pause_started;
        bool cancelled{};
        bool advance{};
        bool traversal_failed{};
        bool interrupted_at_close{};
        auto next_frame = std::chrono::steady_clock::now();
        const auto started = std::chrono::steady_clock::now();
        const auto now_ms = [&started] {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                             started)
                .count();
        };
        const auto elapsed_ms = [&clock](const std::uint32_t frame) {
            return static_cast<std::uint64_t>(clock.at(frame).elapsed_ms);
        };
        const auto emit_event = [&](const std::string &control, const std::uint32_t before,
                                    const std::uint32_t after, const std::uint64_t visit) {
            if (!protocol_v11)
                return;
            const qa::InspectionEvent event{++event_sequence, control, before, after, visit,
                                            elapsed_ms(after)};
            (void)writer.push_fact(
                qa::make_inspection_event_fact(current_run_id, ++inspection_sequence, event));
        };
        // Spec 002, plan §5.9 (RF-7.4, RF-7.5, RF-7.6): every produced or recovered frame is a
        // new record with its visit, copied from the observation of that frame.
        const auto record_visit = [&](const std::uint32_t frame, const ri::FrameContext context) {
            const auto report =
                presentation ? presentation->last_draw_report()
                             : std::optional<ayther::engine::render_observation::DrawReport>{};
            render_observation.valid = false;
            session->publish_render_observation(report ? &*report : nullptr);
            const bool shown =
                presentation && presentation->ready() && presentation->composed_at().has_value();
            const double composed_ms = shown ? std::chrono::duration<double, std::milli>(
                                                   presentation->composed_at()->time_since_epoch())
                                                   .count()
                                             : produced_ms;
            auto measured =
                measurer.measure({frame, input_started_ms, composed_ms, clock_ms()}, context);
            // Without a presented image there is no interval between presentations.
            if (!shown)
                measured.fps_instant.reset();
            if (!protocol_v11)
                return;
            const auto &observed = render_observation.observation;
            qa::RenderFrameRecord record;
            record.frame = frame;
            record.visit = visits[frame];
            record.not_composable_reason =
                render_observation.valid ? not_composable_reason(observed.composability) : "";
            record.occurrences = render_observation.valid
                                     ? std::max<std::uint64_t>(observed.occurrences_total,
                                                               observed.occurrences.size())
                                     : 0U;
            record.processing_ms = measured.processing_ms;
            record.fps_instant = measured.fps_instant;
            (void)writer.push_fact(
                qa::make_render_frame_fact(current_run_id, ++inspection_sequence, record));
            // Plan §5.9: the record is copied as soon as the frame is produced.
            if (presentation) {
                ri::FrameGeneral general{
                    options.rom_path, result.take_id,
                    options.pack_path.empty() ? std::optional<std::string>{}
                                              : std::optional<std::string>{options.pack_path},
                    std::string{
                        phase_code(controller.phase(),
                                   confirmed_linear && controller.position() == frames - 1U)},
                    frames};
                current_record = ri::make_frame_record(
                    frame, static_cast<std::uint32_t>(visits[frame]), std::move(general),
                    render_observation.valid ? render_observation.observation
                                             : ri::render::RenderObservation{},
                    measured);
                notice_text.clear();
                push_debug();
            }
        };
        const auto show = [&](const std::uint32_t frame, const bool linear) {
            if (!presentation || stepper.view == nullptr)
                return;
            const auto shown = presentation->present(*session, *stepper.view, frame, linear);
            // A window that never opened has nothing to interrupt: the take continues and
            // reports its presentation as incomplete, as before (RNF-6).
            if (presentation->ready())
                health.frame_result(shown);
        };

        // Spec 002 (plan §5.11, RF-2.9): the session froze with the confirmed result; navigating
        // after the natural end opens a post-end inspection on a session re-armed from the
        // checkpoints. Its production is silent and its facts belong to the new run.
        const auto rearm = [&]() -> bool {
            ++post_end_runs;
            current_run_id = result.run_id + "-inspection-" + std::to_string(post_end_runs);
            if (protocol_v11)
                (void)writer.send_status([&](const std::uint64_t sequence) {
                    return qa::encode_run_opened({current_run_id, options.qa_take_position},
                                                 sequence);
                });
            event_sequence = 0;
            inspection_sequence = 0;
            try {
                // The synthetic and libretro cores keep one global state: one session at a time.
                session.reset();
                auto rearmed = AytherSession::create(config);
                if (!rearmed)
                    return false;
                session = std::move(*rearmed);
                // The same preparation as the take: its initial state and a fresh HD boundary,
                // so that the checkpoints of the take restore into it.
                const auto again = qa::prepare_recording_replay(
                    recording, layout.layout, session.get(), qa::restore_engine_game_state);
                if (!again.inputs || !again.restore.succeeded)
                    return false;
                const auto fresh = qa::initialize_engine_hd_audio(
                    session.get(), digest_id(result.initial_game_state), nullptr);
                if (fresh.initialization != qa::HdInitialization::fresh)
                    return false;
                stepper.session = session.get();
                stepper.view = nullptr;
                session_frame.reset();
                if (!options.pack_path.empty() && session->has_pack())
                    session->load_audio_events_from_pack();
                if (!active_profile.empty())
                    (void)session->set_profile(active_profile);
                if (subsystem_mask)
                    session->set_subsystems_enabled_mask(*subsystem_mask);
                session->set_audio_runtime_substitution(true);
                audible = true;
                set_audible(false);
                return true;
            } catch (...) {
                return false;
            }
        };

        // Spec 002 (plan §5.6): reaches `target` silently and presents it.
        const auto recover = [&](const std::uint32_t target) -> ri::RecoveryOutcome {
            const auto confirmed = controller.position().value_or(0U);
            if (confirmed_linear && post_end_runs == 0U && !rearm())
                return ri::RecoveryOutcome::failed_unrecoverable;
            if (!session)
                return ri::RecoveryOutcome::failed_unrecoverable;
            const auto recovery_started = std::chrono::steady_clock::now();
            const auto reach = [&](const std::uint32_t goal) {
                // With the position of the session unknown, only a checkpoint can reach the goal.
                const auto plan = [&] {
                    if (session_frame)
                        return ri::plan_recovery(checkpoints.ring(), *session_frame, goal);
                    const auto checkpoint =
                        checkpoints.ring().latest_at_most(static_cast<std::int64_t>(goal) - 1);
                    return checkpoint
                               ? ri::RecoveryPlan{ri::RecoveryKind::restore, *checkpoint,
                                                  static_cast<std::uint32_t>(*checkpoint + 1), goal}
                               : ri::RecoveryPlan{};
                }();
                if (plan.kind == ri::RecoveryKind::impossible)
                    return false;
                std::uint32_t from = plan.replay_from;
                if (plan.kind == ri::RecoveryKind::step) {
                    if (session_frame != goal - 1U)
                        return false;
                } else {
                    const auto restored =
                        checkpoints.restore(*session, plan.checkpoint, post_end_runs == 0U);
                    if (restored != CheckpointStore::RestoreResult::restored) {
                        // A rejected core leaves the session untouched; any later rejection
                        // leaves it in an unknown state.
                        if (restored != CheckpointStore::RestoreResult::missing &&
                            restored != CheckpointStore::RestoreResult::core_rejected) {
                            session_frame.reset();
                            stepper.view = nullptr;
                        }
                        return false;
                    }
                    session_frame = plan.checkpoint < 0
                                        ? std::nullopt
                                        : std::optional<std::uint32_t>{
                                              static_cast<std::uint32_t>(plan.checkpoint)};
                    stepper.view = nullptr;
                }
                for (auto frame = from; frame <= plan.replay_to; ++frame) {
                    if (!produce(frame))
                        return false;
                    if (std::chrono::steady_clock::now() - recovery_started > recovery_limit)
                        return false;
                }
                return session_frame == goal;
            };
            // C4: production in pause is silent; the transport stays paused.
            set_audible(false);
            session->resume_transport();
            const bool reached = reach(target);
            (void)session->pause_after_drain(pause_drain_limit);
            if (reached) {
                ++visits[target];
                show(target, false);
                timing.mark("recovered_presented", clock_ms(), target);
                record_visit(target, ri::FrameContext::navigation);
                return ri::RecoveryOutcome::presented;
            }
            // RF-5.6: back to the last confirmed position, or the traversal fails.
            session->resume_transport();
            const bool restored = session_frame == confirmed || reach(confirmed);
            (void)session->pause_after_drain(pause_drain_limit);
            if (restored) {
                show(confirmed, false);
                return ri::RecoveryOutcome::failed_restored;
            }
            return ri::RecoveryOutcome::failed_unrecoverable;
        };

        // Spec 002 (plan §5.11, contracts.md C1-5): closes the production of the traversal and
        // sends its terminal. `keep_window` confirms the linear result of the last take and
        // keeps the window paused at N−1 (RF-2.8); otherwise the session closes as well.
        bool paused_at_end{};
        const auto finish = [&](const bool keep_window) -> int {
            if (pause_started) {
                user_pause_ms += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - *pause_started)
                        .count());
                pause_started.reset();
            }
            // RF-2.8: a take that reached its natural end ends naturally, even if the window
            // is closed afterwards in the pause at N−1.
            const bool natural = controller.ended_naturally();
            const bool stopped_early = (cancelled && !natural) || traversal_failed;
            if (presentation && presentation->ready())
                result.presentation.cancelled = stopped_early;
            const bool inspected = controller.traversal_inspected();

            // Plan §5.11: a natural end consumed every input; the boundary is checked.
            set_audible(true);
            session->resume_transport();
            if (natural && loop.frames_completed() == frames) {
                const auto exhausted = loop.execute_next(operations);
                if (exhausted.error != qa::ReplayFrameError::input_exhausted) {
                    result.inputs_consumed = loop.frames_completed();
                    return fail_session("recording_input_boundary_failed");
                }
            }
            result.inputs_consumed = loop.frames_completed();
            // A traversal that stopped away from the end of linear production has no linear
            // boundary to check: the close only freezes, finalizes and drains.
            qa::ReplayProgressPublisher inspection_close{frames, nullptr, acknowledge_progress};
            (void)inspection_close.publish_acceptance();
            const bool off_boundary =
                session_frame && *session_frame + 1U != loop.frames_completed();
            const auto close_operations =
                qa::engine_replay_production_close_operations(session.get());
            auto closed =
                qa::close_replay_production(off_boundary ? inspection_close : progress,
                                            close_operations, stopped_early || off_boundary);
            if (closed.error != qa::ReplayProductionCloseError::none)
                return fail_session("audio_production_close_failed");
            std::vector<std::uint8_t> final_state;
            if (!session->serialize(final_state))
                return fail_session("final_state_serialize_failed");
            result.final_game_state = state_identity(final_state);
            if (!keep_window) {
                (void)controller.cancel();
                report_state(true);
            }

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
                const auto now = static_cast<std::uint64_t>(elapsed);
                const auto current_output_end = writer.last_pcm_end();
                if (!close_watchdog.observe_bytes_received(current_output_end, now) ||
                    !close_watchdog.observe_bytes_durable(current_output_end, now) ||
                    close_watchdog.check(now).has_value()) {
                    return fail_session("pcm_capture_close_timeout");
                }
            }
            if (keep_window) {
                // The frozen session only keeps the image of N−1: nothing more is evidence.
                gate.set_silent(true);
                stop_observation();
            } else {
                stop_capture();
            }
            writer.set_pcm_limit(closed.drain.output_sample_limit);
            if (!writer.pcm_complete())
                return fail_session("pcm_capture_close_timeout");

            // Spec 002 (contracts.md C1-5): how the playback ended, the traversal and the
            // pause.
            const auto describe = [&] {
                qa::describe_linear_terminal(result, stopped_early && !traversal_failed);
                if (traversal_failed)
                    result.playback = "failed";
                else if (stopped_early && interrupted_at_close)
                    result.playback = "interrupted";
                if (inspected) {
                    result.traversal = "inspection";
                    result.linear_completed = false;
                }
                result.ended_paused = natural && paused_at_end;
                result.user_pause_ms = user_pause_ms;
                result.interruptions = health.incidents();
            };
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
                // Spec 002 (RF-2.4): a cancellation stays a cancellation; what it left
                // incomplete is a reason of its evidence.
                if (stopped_early && !traversal_failed) {
                    result.evidence_reasons.push_back(result.code);
                    result.code = "replay_cancelled";
                }
                describe();
                return writer.send_terminal(result) ? qa_execution_error : qa_protocol_error;
            }
            result.succeeded = !stopped_early && result.presentation.complete();
            result.code = traversal_failed   ? "inspection_recovery_failed"
                          : stopped_early    ? "replay_cancelled"
                          : result.succeeded ? "replay_evidence_streamed"
                                             : "presentation_incomplete";
            describe();
            return writer.send_terminal(result) ? (result.succeeded ? 0 : qa_execution_error)
                                                : qa_protocol_error;
        };
        std::optional<int> confirmed_exit;

        bool done{};
        std::function<void(ri::Commands)> apply;
        apply = [&](ri::Commands commands) {
            for (const auto &command : commands) {
                switch (command.kind) {
                case ri::CommandKind::present_frame:
                    if (session_frame == command.frame)
                        show(command.frame, false);
                    break;
                case ri::CommandKind::recover_frame:
                    break;
                case ri::CommandKind::notice:
                    notice_text = notice_of(command.notice);
                    push_debug();
                    if (session_frame && controller.phase() != ri::InspectionPhase::playing)
                        show(*session_frame, false);
                    break;
                case ri::CommandKind::pause_audio:
                    if (command.frame + 1U == frames)
                        paused_at_end = true;
                    if (confirmed_linear)
                        break;
                    {
                        // RF-4.1, P-9: the audio already produced plays to the end of the frame.
                        timing.mark("paused_presented", clock_ms(), command.frame);
                        const auto drain_started = clock_ms();
                        const auto drained = session->pause_after_drain(pause_drain_limit);
                        timing.mark("pause_drain", clock_ms() - drain_started,
                                    static_cast<int>(drained.code), drained.remaining_frames);
                    }
                    pause_started = std::chrono::steady_clock::now();
                    break;
                case ri::CommandKind::resume_audio:
                    // RF-4.4, RF-4.6: the cadence restarts now; the pause is user time.
                    set_audible(true);
                    session->resume_transport();
                    if (pause_started)
                        user_pause_ms += static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - *pause_started)
                                .count());
                    pause_started.reset();
                    next_frame = std::chrono::steady_clock::now();
                    after_resume = true;
                    break;
                case ri::CommandKind::toggle_debug:
                    debug_visible = controller.debug_visible();
                    push_debug();
                    emit_event("overlay_toggle", controller.position().value_or(0U),
                               controller.position().value_or(0U),
                               visits[controller.position().value_or(0U)]);
                    if (session_frame && controller.phase() == ri::InspectionPhase::paused)
                        show(*session_frame, false);
                    break;
                case ri::CommandKind::inspection_event: {
                    // A step names the visit it opens; the others, the current one.
                    const bool step =
                        command.control == "step_back" || command.control == "step_forward";
                    emit_event(command.control,
                               step ? controller.position().value_or(0U) : command.frame,
                               command.frame, visits[command.frame] + (step ? 1U : 0U));
                    break;
                }
                case ri::CommandKind::confirm_linear_result:
                    // RF-2.8: the linear result is confirmed and sent before the pause at N−1.
                    paused_at_end = true;
                    confirmed_exit = finish(true);
                    confirmed_linear = true;
                    // C1-3: the window stays paused at N−1 after the confirmed result.
                    report_state(true);
                    if (!session)
                        done = true;
                    break;
                case ri::CommandKind::advance_take:
                    advance = true;
                    done = true;
                    break;
                case ri::CommandKind::finish_traversal:
                    traversal_failed = true;
                    done = true;
                    break;
                case ri::CommandKind::close:
                    done = true;
                    break;
                }
            }
        };

        apply(controller.prepared());
        report_state(true);
        std::vector<PresentationEvent> events;
        while (!done) {
            // C1-1, RF-2.5: a cancellation is attended before any frame or pending control.
            if (supervisor_cancelled(*control, pending->request_id, result.run_id)) {
                interrupted_at_close = controller.phase() == ri::InspectionPhase::interrupted;
                cancelled = true;
                apply(controller.cancel());
                break;
            }
            events.clear();
            // A scripted action reaches the window first, so that it is read in this poll.
            if (script) {
                const ri::ScriptState state{controller.position(),
                                            controller.phase() == ri::InspectionPhase::paused,
                                            now_ms()};
                for (const auto &action : script->due(state)) {
                    if (action.kind == ri::ScriptActionKind::corrupt_checkpoints) {
                        checkpoints.corrupt_all_for_test();
                    } else if (action.kind == ri::ScriptActionKind::corrupt_visual_state) {
                        checkpoints.corrupt_visual_states_for_test();
                    } else if (presentation) {
                        presentation->deliver(action);
                    } else {
                        using Kind = ri::ScriptActionKind;
                        switch (action.kind) {
                        case Kind::key_down:
                        case Kind::key_repeat:
                            events.push_back({PresentationEvent::Kind::key_down, action.key,
                                              action.kind == Kind::key_repeat, true});
                            break;
                        case Kind::key_up:
                            events.push_back({PresentationEvent::Kind::key_up, action.key});
                            break;
                        case Kind::focus_lost:
                        case Kind::focus_gained:
                            events.push_back({PresentationEvent::Kind::focus, ri::Key::other, false,
                                              action.kind == Kind::focus_gained});
                            break;
                        case Kind::close:
                            events.push_back({PresentationEvent::Kind::close});
                            break;
                        default:
                            // Without a window there is no presentation to interrupt.
                            break;
                        }
                    }
                }
            }
            if (presentation)
                presentation->poll_events(events);
            for (const auto &event : events) {
                switch (event.kind) {
                case PresentationEvent::Kind::key_down:
                    if (const auto action = router.key_down(event.key, event.repeat)) {
                        timing.mark("key", clock_ms(), static_cast<int>(*action),
                                    phase_code(controller.phase(), false));
                        if (*action == ri::KeyAction::toggle &&
                            controller.phase() == ri::InspectionPhase::paused)
                            resume_key_ms = clock_ms();
                        apply(controller.key(*action));
                    }
                    break;
                case PresentationEvent::Kind::key_up:
                    router.key_up(event.key);
                    break;
                case PresentationEvent::Kind::focus:
                    router.focus(event.value);
                    health.focus(event.value);
                    break;
                case PresentationEvent::Kind::minimized:
                    health.minimized(event.value);
                    break;
                case PresentationEvent::Kind::audio_removed:
                    health.audio_device_removed(event.value);
                    break;
                case PresentationEvent::Kind::audio_added:
                    health.audio_device_added();
                    break;
                case PresentationEvent::Kind::scroll:
                    if (presentation) {
                        presentation->scroll_debug(event.scroll);
                        if (session_frame && controller.phase() != ri::InspectionPhase::playing)
                            show(*session_frame, false);
                    }
                    break;
                case PresentationEvent::Kind::close:
                    // RF-2.10: closing the window is a cancellation.
                    interrupted_at_close = controller.phase() == ri::InspectionPhase::interrupted;
                    cancelled = true;
                    apply(controller.cancel());
                    break;
                }
                if (done)
                    break;
            }
            if (done)
                break;

            // Plan §5.10: a loss stops the take at the last completed frame.
            if (health.state() != ri::PresentationState::healthy &&
                controller.phase() != ri::InspectionPhase::interrupted &&
                controller.phase() != ri::InspectionPhase::closing) {
                apply(controller.presentation_interrupted(health.cause()));
                notice_text = notice_of(ri::Notice::interrupted);
                push_debug();
            }
            if (controller.phase() == ri::InspectionPhase::interrupted) {
                const bool video = presentation ? presentation->recover_video() : true;
                const bool audio = presentation ? presentation->audio_ready() : true;
                if (health.poll(video, audio) == ri::RecoveryAttempt::recovered)
                    apply(controller.presentation_recovered());
            }
            report_state(false);
            if (done)
                break;

            switch (controller.phase()) {
            case ri::InspectionPhase::playing:
            case ri::InspectionPhase::pausing: {
                const auto frame = controller.next_frame();
                set_audible(true);
                if (!produce(frame)) {
                    result.inputs_consumed = loop.frames_completed();
                    return fail_session("recording_replay_failed");
                }
                ++visits[frame];
                show(frame, true);
                if (after_resume && resume_key_ms) {
                    timing.mark("resumed_presented", clock_ms(), frame,
                                stepper.view != nullptr ? stepper.view->frame_index : 0U);
                    resume_key_ms.reset();
                }
                if (presentation)
                    timing.mark("overlay", controller.debug_visible() ? 1 : 0,
                                presentation->last_overlay_ms());
                record_visit(frame, after_resume ? ri::FrameContext::after_pause
                                                 : ri::FrameContext::continuous);
                after_resume = false;
                if (inspectable && checkpoints.should_capture(frame) &&
                    frame + 1U == loop.frames_completed())
                    (void)checkpoints.capture(*session, frame);
                apply(controller.frame_completed(frame));
                if (controller.phase() == ri::InspectionPhase::playing) {
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
                break;
            }
            case ri::InspectionPhase::recovering: {
                report_state(true);
                // P-4: the notice «recovering» is visible while the frame is rebuilt.
                notice_text =
                    std::string{ri::replay_message(replay_language, ri::ReplayMessage::recovering)};
                push_debug();
                if (session_frame)
                    show(*session_frame, false);
                notice_text.clear();
                push_debug();
                const auto target = controller.target().value_or(0U);
                const auto outcome = recover(target);
                if (outcome != ri::RecoveryOutcome::presented)
                    emit_event("recover_failed", target, controller.position().value_or(0U),
                               visits[controller.position().value_or(0U)]);
                apply(controller.recovery_finished(outcome));
                report_state(true);
                break;
            }
            default:
                // Paused or interrupted: the window stays responsive.
                if (presentation)
                    show(session_frame.value_or(0U), false);
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
                break;
            }
        }
        const int exit_code = confirmed_exit ? *confirmed_exit : finish(false);
        if (confirmed_exit) {
            // RF-2.9: closing the window or cancelling after the confirmed result only closes
            // the window, and the post-end inspection if one was opened; the result does not
            // change.
            (void)controller.cancel();
            report_state(true);
            if (post_end_runs != 0U) {
                auto inspection = result;
                inspection.run_id = current_run_id;
                inspection.traversal = "post_end_inspection";
                inspection.playback = "natural_end";
                inspection.linear_completed = false;
                inspection.succeeded = false;
                inspection.code = "post_end_inspection_closed";
                inspection.evidence_reasons = {"post_end_inspection"};
                inspection.ended_paused = controller.position() == frames - 1U;
                inspection.user_pause_ms = 0U;
                inspection.trace = {};
                inspection.trace.observed_fact_count = inspection_sequence;
                inspection.trace.loss_free = true;
                (void)writer.flush(false);
                (void)writer.send_terminal(inspection);
            }
            stop_capture();
        }
        // Every audio producer stopped with the session: the marks can be read.
        if (audio_tap) {
            const auto boundaries =
                (std::min)(audio_tap->boundary_count.load(), AudioTimingTap::capacity);
            for (std::size_t index = 0; index < boundaries; ++index) {
                const auto &boundary = audio_tap->boundaries[index];
                timing.mark("frame_boundary", boundary.at_ms, boundary.emulation_frame,
                            boundary.output_position, boundary.sample_rate, boundary.valid ? 1 : 0);
            }
            const auto blocks = (std::min)(audio_tap->block_count.load(), AudioTimingTap::capacity);
            for (std::size_t index = 0; index < blocks; ++index)
                timing.mark("pcm_block", audio_tap->blocks[index].at_ms,
                            audio_tap->blocks[index].begin, audio_tap->blocks[index].end,
                            audio_tap->blocks[index].sample_rate);
            timing.mark("audio_tap", audio_tap->boundary_count.load(),
                        audio_tap->block_count.load());
        }
        return exit_code;
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
