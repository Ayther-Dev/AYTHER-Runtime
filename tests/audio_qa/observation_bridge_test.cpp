#include "observation_bridge.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace obs = ayther::engine::audio_observation;
namespace {

void require(const bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct Capture final {
    bool valid{true};
    std::string fact_run;
    std::string fact_kind;
    std::string fact_context;
    std::string fact_state;
    std::string fact_field_name;
    std::string fact_field_value;
    std::string pcm_run;
    std::string pcm_point;
    std::string pcm_timeline;
    std::string pcm_context;
    std::vector<std::byte> pcm_bytes;

    static void fact(void *value, const std::string_view run_id,
                     const qa::EngineFactView &view) noexcept {
        auto &capture = *static_cast<Capture *>(value);
        try {
            capture.fact_run = run_id;
            capture.fact_kind = view.kind;
            capture.fact_context = std::get<obs::PreexistingContext>(view.causes.front()).state_id;
            capture.fact_state = view.state_orders.front().state_id;
            capture.fact_field_name = view.fields.front().name;
            capture.fact_field_value = std::get<std::string_view>(view.fields.front().value);
            capture.valid = capture.valid && view.id == obs::FactId{3, 7} &&
                            view.frame.availability == obs::Availability::known &&
                            view.frame.emulation_frame == 42;
        } catch (...) {
            capture.valid = false;
        }
    }

    static void pcm(void *value, const std::string_view run_id,
                    const qa::EnginePcmView &view) noexcept {
        auto &capture = *static_cast<Capture *>(value);
        try {
            capture.pcm_run = run_id;
            capture.pcm_point = view.capture_point;
            capture.pcm_timeline = view.range.timeline;
            capture.pcm_context = std::get<obs::PreexistingContext>(view.causes.front()).state_id;
            capture.pcm_bytes.assign(view.bytes.begin(), view.bytes.end());
            capture.valid = capture.valid && view.id == obs::FactId{2, 9} &&
                            view.range.sample_rate == 44100 && view.range.begin == 100 &&
                            view.range.end == 102 && view.format == obs::PcmFormat::s16_le &&
                            view.channels == 2;
        } catch (...) {
            capture.valid = false;
        }
    }
};

struct PcmCount final {
    std::size_t value{};

    static void consume(void *context, std::string_view, const qa::EnginePcmView &) noexcept {
        ++static_cast<PcmCount *>(context)->value;
    }
};

struct FactCount final {
    std::size_t value{};

    static void consume(void *context, std::string_view, const qa::EngineFactView &) noexcept {
        ++static_cast<FactCount *>(context)->value;
    }
};

} // namespace

int main() {
    try {
        using Bridge = qa::RuntimeObservationBridge<4, 2, 1>;
        auto bridge = std::make_unique<Bridge>("run-95");
        require(bridge->valid() && bridge->run_id() == "run-95", "bridge_run_id_rejected");
        const auto observer = bridge->observer();
        require(observer.on_fact != nullptr && observer.on_pcm != nullptr,
                "bridge_observer_unavailable");

        std::string fact_kind = "voice_started";
        std::string fact_context = "initial-audio";
        std::string state_id = "mixer-voices";
        std::string field_name = "assignment_id";
        std::string field_value = "assignment-4";
        std::array<obs::Cause, 1> fact_causes{obs::PreexistingContext{fact_context}};
        std::array<obs::StateOrder, 1> orders{{state_id, 11}};
        std::array<obs::FieldView, 1> fields{{field_name,
                                              obs::Availability::known,
                                              obs::Unit::none,
                                              std::string_view{field_value},
                                              {}}};
        observer.observe(obs::FactView{
            {3, 7}, fact_kind, {obs::Availability::known, 42, {}}, fact_causes, orders, fields});

        std::string pcm_point = "session_postmix";
        std::string pcm_timeline = "engine_main_output";
        std::string pcm_context = "initial-output";
        std::array<std::byte, 8> pcm_bytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                           std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
        const auto expected_pcm = pcm_bytes;
        std::array<obs::Cause, 1> pcm_causes{obs::PreexistingContext{pcm_context}};
        observer.observe(obs::PcmView{{2, 9},
                                      pcm_point,
                                      {pcm_timeline, 44100, 100, 102},
                                      obs::PcmFormat::s16_le,
                                      2,
                                      pcm_bytes,
                                      pcm_causes});

        fact_kind.assign(fact_kind.size(), 'x');
        fact_context.assign(fact_context.size(), 'x');
        state_id.assign(state_id.size(), 'x');
        field_name.assign(field_name.size(), 'x');
        field_value.assign(field_value.size(), 'x');
        pcm_point.assign(pcm_point.size(), 'x');
        pcm_timeline.assign(pcm_timeline.size(), 'x');
        pcm_context.assign(pcm_context.size(), 'x');
        pcm_bytes.fill(std::byte{0xff});

        Capture capture;
        require(bridge->try_consume_fact(&capture, Capture::fact), "owned_fact_not_delivered");
        require(bridge->try_consume_pcm(&capture, Capture::pcm), "owned_pcm_not_delivered");
        require(capture.valid && capture.fact_run == "run-95" && capture.pcm_run == "run-95" &&
                    capture.fact_kind == "voice_started" &&
                    capture.fact_context == "initial-audio" &&
                    capture.fact_state == "mixer-voices" &&
                    capture.fact_field_name == "assignment_id" &&
                    capture.fact_field_value == "assignment-4" &&
                    capture.pcm_point == "session_postmix" &&
                    capture.pcm_timeline == "engine_main_output" &&
                    capture.pcm_context == "initial-output" &&
                    capture.pcm_bytes ==
                        std::vector<std::byte>{expected_pcm.begin(), expected_pcm.end()},
                "borrowed_engine_views_outlived_callback");
        require(bridge->losses() == qa::ObservationBridgeLosses{},
                "bridge_reported_unexpected_loss");

        auto invalid = std::make_unique<Bridge>("");
        require(!invalid->valid() && invalid->observer().on_fact == nullptr &&
                    invalid->observer().on_pcm == nullptr,
                "invalid_run_id_exposed_observer");

        auto production = std::make_unique<qa::ProductionObservationBridge>("run-main-output");
        const auto production_observer = production->observer();
        constexpr std::size_t callback_count = 2048;
        constexpr std::uint64_t callback_frames = 64;
        std::array<std::byte, callback_frames * 2U * sizeof(float)> production_bytes{};
        for (std::size_t index = 0; index < callback_count; ++index) {
            const auto begin = static_cast<std::uint64_t>(index) * callback_frames;
            production_observer.observe(
                obs::PcmView{{7, static_cast<std::uint64_t>(index + 1U)},
                             "sdl_logical_device_postmix",
                             {"engine_main_output", 44100, begin, begin + callback_frames},
                             obs::PcmFormat::f32_le,
                             2,
                             production_bytes,
                             {}});
        }
        require(production->losses().full_pcm == 0U && production->losses().invalid_pcm == 0U,
                "main_output_pcm_backlog_not_preserved");
        PcmCount production_pcm;
        while (production->try_consume_pcm(&production_pcm, PcmCount::consume)) {
        }
        require(production_pcm.value == callback_count, "main_output_pcm_burst_not_preserved");

        production_observer.observe(obs::FactView{{8, 1}, "main_mix_output_span", {}, {}, {}, {}});
        FactCount production_facts;
        require(production->losses().invalid_producer == 0U &&
                    production->try_consume_fact(&production_facts, FactCount::consume) &&
                    production_facts.value == 1U,
                "observation_api_1_1_main_mix_producers_rejected");

        auto visible_bridge = std::make_unique<qa::VisibleObservationBridge>("run-physical-output");
        const auto visible_observer = visible_bridge->observer();
        std::vector<std::byte> physical_bytes(obs::max_pcm_bytes);
        visible_observer.observe(
            obs::PcmView{{7, 1},
                         "sdl_logical_device_postmix",
                         {"engine_main_output", 192000, 0, obs::max_pcm_bytes / 32U},
                         obs::PcmFormat::f32_le,
                         8,
                         physical_bytes,
                         {}});
        PcmCount physical_pcm;
        require(visible_bridge->losses().invalid_pcm == 0U &&
                    visible_bridge->try_consume_pcm(&physical_pcm, PcmCount::consume) &&
                    physical_pcm.value == 1U,
                "physical_device_pcm_block_was_rejected");

        auto saturated =
            std::make_unique<qa::RuntimeObservationBridge<4, 1, 1>>("run-saturated-producer");
        const auto saturated_observer = saturated->observer();
        saturated_observer.observe(obs::FactView{{4, 1}, "first", {}, {}, {}, {}});
        saturated_observer.observe(obs::FactView{{4, 2}, "second", {}, {}, {}, {}});
        require(saturated->losses().full_fact == 1U && saturated->first_full_fact_producer() == 4U,
                "full_fact_producer_not_identified");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "observation_bridge_test: %s\n", error.what());
        return 1;
    }
}
