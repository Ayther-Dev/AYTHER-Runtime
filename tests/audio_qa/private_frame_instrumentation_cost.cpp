#include "engine_game_state_restore.h"
#include "engine_hd_initialization.h"
#include "recording_header.h"
#include "recording_replay_preparation.h"

#include <ayther/ayther_session.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace qa = ayther::audio_qa;
namespace obs = ayther::engine::audio_observation;
namespace {

constexpr std::size_t pair_count = 3U;
constexpr std::size_t repetitions_per_condition = 3U;
constexpr std::uint64_t p95_ratio_numerator = 105U;
constexpr std::uint64_t p95_ratio_denominator = 100U;
constexpr std::uint64_t p99_delta_limit_nanoseconds = 1'000'000U;

void require(const bool condition, const char *const message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<std::byte> read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "recording_open_failed");
    const std::vector<char> characters{std::istreambuf_iterator<char>{input},
                                       std::istreambuf_iterator<char>{}};
    require(!input.bad() && !characters.empty(), "recording_read_failed");
    std::vector<std::byte> bytes(characters.size());
    std::memcpy(bytes.data(), characters.data(), characters.size());
    return bytes;
}

bool configure_measurement_thread() noexcept {
#if defined(_WIN32)
    const auto processor_count = GetActiveProcessorCount(0U);
    if (processor_count == 0U || processor_count > sizeof(DWORD_PTR) * 8U)
        return false;
    const auto processor = processor_count > 2U ? 2U : processor_count - 1U;
    const auto affinity = static_cast<DWORD_PTR>(1) << processor;
    return SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS) != 0 &&
           SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) != 0 &&
           SetThreadAffinityMask(GetCurrentThread(), affinity) != 0;
#else
    return true;
#endif
}

struct FactSink {
    std::uint64_t facts{};

    static void receive(void *const context, const obs::FactView &fact) noexcept {
        auto &sink = *static_cast<FactSink *>(context);
        static_cast<void>(fact);
        ++sink.facts;
    }
};

struct RunResult {
    std::vector<std::uint64_t> work_ticks;
    std::vector<std::uint64_t> frame_indices;
    std::vector<std::uint8_t> final_state;
    std::array<std::uint64_t, 15> decisions{};
    std::uint64_t assignments{};
    std::uint64_t facts{};
    bool valid{};
};

std::uint64_t now_measurement_ticks() noexcept {
#if defined(_WIN32)
    ULONG64 cycles{};
    if (QueryThreadCycleTime(GetCurrentThread(), &cycles) == 0)
        return 0U;
    return cycles;
#else
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
#endif
}

double measurement_ticks_per_nanosecond() noexcept {
#if defined(_WIN32)
    LARGE_INTEGER frequency{};
    LARGE_INTEGER started{};
    LARGE_INTEGER current{};
    ULONG64 cycles_started{};
    ULONG64 cycles_finished{};
    if (QueryPerformanceFrequency(&frequency) == 0 || frequency.QuadPart <= 0 ||
        QueryPerformanceCounter(&started) == 0 ||
        QueryThreadCycleTime(GetCurrentThread(), &cycles_started) == 0)
        return 0.0;
    const auto target_ticks = frequency.QuadPart / 5;
    do {
        if (QueryPerformanceCounter(&current) == 0)
            return 0.0;
    } while (current.QuadPart - started.QuadPart < target_ticks);
    if (QueryThreadCycleTime(GetCurrentThread(), &cycles_finished) == 0 ||
        cycles_finished <= cycles_started)
        return 0.0;
    const auto elapsed_nanoseconds = static_cast<double>(current.QuadPart - started.QuadPart) *
                                     1.0e9 / static_cast<double>(frequency.QuadPart);
    return static_cast<double>(cycles_finished - cycles_started) / elapsed_nanoseconds;
#else
    return 1.0;
#endif
}

std::optional<RunResult> run(const std::filesystem::path &core, const std::filesystem::path &rom,
                             const std::filesystem::path &pack,
                             const std::filesystem::path &trust_registry,
                             const std::span<const std::byte> recording,
                             const qa::RecordingLayout &layout, const bool observed) {
    FactSink sink;
    ayther::AytherSession::Config config;
    config.core_path = core.string();
    config.rom_path = rom.string();
    config.pack_path = pack.string();
    config.trust_registry = trust_registry.string();
    config.enable_audio = false;
    config.derive_core_pack = false;
    config.core_options.emplace_back("no_sprite_limit", "enabled");
    if (observed) {
        config.audio_observer = {&sink, FactSink::receive, nullptr};
    }

    auto created = ayther::AytherSession::create(config);
    if (!created)
        return std::nullopt;
    auto session = std::move(*created);
    auto prepared = qa::prepare_recording_replay(recording, layout, session.get(),
                                                 qa::restore_engine_game_state);
    if (!prepared.inputs || !prepared.restore.succeeded)
        return std::nullopt;
    const auto hd = qa::initialize_engine_hd_audio(session.get(), "private-frame-cost", nullptr);
    if (hd.initialization != qa::HdInitialization::fresh || hd.evidence_incomplete)
        return std::nullopt;
    session->load_audio_events_from_pack();
    if (session->audio_event_assignment_count() == 0U || !session->set_profile("full"))
        return std::nullopt;
    session->set_audio_runtime_substitution(true);

    RunResult result;
    result.assignments = session->audio_event_assignment_count();
    result.work_ticks.reserve(layout.frame_count);
    result.frame_indices.reserve(layout.frame_count);
    auto inputs = *prepared.inputs;
    for (std::uint32_t frame{}; frame < layout.frame_count; ++frame) {
        const auto input = inputs.next();
        if (!input || input->frame != frame)
            return std::nullopt;
        const auto started = now_measurement_ticks();
        session->set_input(0, input->buttons);
        const auto &view = session->step();
        const auto finished = now_measurement_ticks();
        if (started == 0U || finished < started)
            return std::nullopt;
        result.work_ticks.push_back(finished - started);
        result.frame_indices.push_back(view.frame_index);
    }
    if (!inputs.exhausted() || !session->serialize(result.final_state))
        return std::nullopt;
    session->audio_live_match_stats(&result.decisions[0], &result.decisions[1],
                                    &result.decisions[2], &result.decisions[3]);
    session->audio_unified_stats(&result.decisions[4], &result.decisions[5], &result.decisions[6],
                                 &result.decisions[7]);
    session->audio_fallback_stats(&result.decisions[8], &result.decisions[9]);
    session->audio_resume_stats(&result.decisions[10], &result.decisions[11],
                                &result.decisions[12]);
    result.decisions[13] = session->audio_event_count();
    result.decisions[14] = session->audio_event_assignment_count();
    if (observed) {
        result.facts = sink.facts;
        result.valid = sink.facts > 0U;
    } else {
        result.valid = true;
    }
    return result;
}

std::uint64_t nearest_rank(std::vector<std::uint64_t> values, const std::size_t percentile) {
    require(!values.empty() && percentile > 0U && percentile <= 100U, "invalid_percentile_input");
    std::sort(values.begin(), values.end());
    const auto rank = (percentile * values.size() + 99U) / 100U;
    return values[rank - 1U];
}

struct Percentiles {
    std::uint64_t p95{};
    std::uint64_t p99{};
};

Percentiles percentiles(const RunResult &result) {
    return {nearest_rank(result.work_ticks, 95U), nearest_rank(result.work_ticks, 99U)};
}

bool within_budget(const Percentiles disabled, const Percentiles enabled,
                   const double ticks_per_nanosecond) noexcept {
    return disabled.p95 != 0U &&
           enabled.p95 * p95_ratio_denominator <= disabled.p95 * p95_ratio_numerator &&
           static_cast<double>(enabled.p99) <=
               static_cast<double>(disabled.p99) +
                   static_cast<double>(p99_delta_limit_nanoseconds) * ticks_per_nanosecond;
}

std::uint64_t ticks_to_nanoseconds(const std::uint64_t ticks,
                                   const double ticks_per_nanosecond) noexcept {
    return static_cast<std::uint64_t>(static_cast<double>(ticks) / ticks_per_nanosecond);
}

double work_fps(const RunResult &result, const double ticks_per_nanosecond) noexcept {
    const auto total =
        std::accumulate(result.work_ticks.begin(), result.work_ticks.end(), std::uint64_t{});
    return total == 0U ? 0.0
                       : static_cast<double>(result.work_ticks.size()) * 1.0e9 *
                             ticks_per_nanosecond / static_cast<double>(total);
}

void merge_run(RunResult &aggregate, RunResult sample) {
    const bool first = aggregate.frame_indices.empty();
    if (first) {
        aggregate.final_state = sample.final_state;
        aggregate.decisions = sample.decisions;
        aggregate.assignments = sample.assignments;
        aggregate.valid = sample.valid;
    } else {
        aggregate.valid =
            aggregate.valid && sample.valid && aggregate.final_state == sample.final_state &&
            aggregate.decisions == sample.decisions && aggregate.assignments == sample.assignments;
    }
    aggregate.facts += sample.facts;
    aggregate.work_ticks.insert(aggregate.work_ticks.end(), sample.work_ticks.begin(),
                                sample.work_ticks.end());
    aggregate.frame_indices.insert(aggregate.frame_indices.end(), sample.frame_indices.begin(),
                                   sample.frame_indices.end());
}

} // namespace

int main(const int argc, char **argv) {
    try {
        require(argc == 6, "usage: private_frame_instrumentation_cost core rom pack trust take");
        require(configure_measurement_thread(), "measurement_thread_configuration_failed");
        const auto ticks_per_nanosecond = measurement_ticks_per_nanosecond();
        require(ticks_per_nanosecond > 0.0, "measurement_clock_calibration_failed");
        const std::filesystem::path take{argv[5]};
        const auto recording = read_file(take);
        const auto decoded = qa::decode_recording_layout(recording);
        require(decoded.error == qa::RecordingLayoutError::none, "recording_layout_invalid");
        require(decoded.layout.frame_count > 0U, "recording_has_no_frames");

        const auto warmup_disabled =
            run(argv[1], argv[2], argv[3], argv[4], recording, decoded.layout, false);
        const auto warmup_enabled =
            run(argv[1], argv[2], argv[3], argv[4], recording, decoded.layout, true);
        require(warmup_disabled.has_value() && warmup_enabled.has_value(), "private_warmup_failed");
        require(warmup_disabled->valid && warmup_enabled->valid &&
                    warmup_disabled->final_state == warmup_enabled->final_state &&
                    warmup_disabled->frame_indices == warmup_enabled->frame_indices &&
                    warmup_disabled->decisions == warmup_enabled->decisions &&
                    warmup_disabled->assignments == warmup_enabled->assignments,
                "private_warmup_behavior_mismatch");
        std::printf("private_frame_instrumentation_cost take=%s warmup=passed frames=%zu "
                    "facts=%llu\n",
                    take.filename().string().c_str(), warmup_enabled->frame_indices.size(),
                    static_cast<unsigned long long>(warmup_enabled->facts));

        bool all_passed = true;
        for (std::size_t pair{}; pair < pair_count; ++pair) {
            RunResult disabled;
            RunResult enabled;
            for (std::size_t repetition{}; repetition < repetitions_per_condition; ++repetition) {
                const bool enabled_first = ((pair + repetition) % 2U) != 0U;
                const auto first = run(argv[1], argv[2], argv[3], argv[4], recording,
                                       decoded.layout, enabled_first);
                const auto second = run(argv[1], argv[2], argv[3], argv[4], recording,
                                        decoded.layout, !enabled_first);
                require(first.has_value() && second.has_value(), "private_replay_failed");
                const auto &observed_sample = enabled_first ? *first : *second;
                require(observed_sample.facts == warmup_enabled->facts,
                        "private_observer_fact_count_mismatch");
                merge_run(disabled, enabled_first ? *second : *first);
                merge_run(enabled, enabled_first ? *first : *second);
            }
            const bool behavior = disabled.valid && enabled.valid &&
                                  disabled.final_state == enabled.final_state &&
                                  disabled.frame_indices == enabled.frame_indices &&
                                  disabled.decisions == enabled.decisions &&
                                  disabled.assignments == enabled.assignments;
            const auto disabled_percentiles = percentiles(disabled);
            const auto enabled_percentiles = percentiles(enabled);
            const bool budget = behavior && within_budget(disabled_percentiles, enabled_percentiles,
                                                          ticks_per_nanosecond);
            all_passed = all_passed && budget;
            std::printf(
                "private_frame_instrumentation_cost take=%s pair=%zu repetitions=%zu "
                "order=alternating frames=%zu "
                "assignments=%llu disabled_p95_ns=%llu enabled_p95_ns=%llu "
                "disabled_p99_ns=%llu enabled_p99_ns=%llu disabled_work_fps=%.3f "
                "enabled_work_fps=%.3f facts=%llu losses=%llu decisions_equal=%s "
                "state_equal=%s budget=%s\n",
                take.filename().string().c_str(), pair + 1U, repetitions_per_condition,
                enabled.frame_indices.size(), static_cast<unsigned long long>(enabled.assignments),
                static_cast<unsigned long long>(
                    ticks_to_nanoseconds(disabled_percentiles.p95, ticks_per_nanosecond)),
                static_cast<unsigned long long>(
                    ticks_to_nanoseconds(enabled_percentiles.p95, ticks_per_nanosecond)),
                static_cast<unsigned long long>(
                    ticks_to_nanoseconds(disabled_percentiles.p99, ticks_per_nanosecond)),
                static_cast<unsigned long long>(
                    ticks_to_nanoseconds(enabled_percentiles.p99, ticks_per_nanosecond)),
                work_fps(disabled, ticks_per_nanosecond), work_fps(enabled, ticks_per_nanosecond),
                static_cast<unsigned long long>(enabled.facts), static_cast<unsigned long long>(0U),
                disabled.decisions == enabled.decisions ? "true" : "false",
                disabled.final_state == enabled.final_state ? "true" : "false",
                budget ? "passed" : "failed");
        }
        std::printf("private_frame_instrumentation_cost take=%s pairs=%zu result=%s\n",
                    take.filename().string().c_str(), pair_count, all_passed ? "passed" : "failed");
        return all_passed ? 0 : 1;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "private_frame_instrumentation_cost: %s\n", error.what());
        return 2;
    }
}
