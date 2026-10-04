// Spec 002, with the Engine session of the QA package and the synthetic core:
//   BR-139 (RF-3.6; plan §4.4, §8 P-12) during linear playback a checkpoint of the core, the
//          visual state and the HD audio state is kept every K = 60 frames, the initial state is
//          always kept, and everything fits the budget; a smaller budget drops the oldest.
//   BR-155 (RF-3.6, RF-5.2; oracle O3) for every k of a synthetic take of 600 frames, recovering
//          k (restore the latest checkpoint at or before k − 1 and re-simulate silently with the
//          recorded inputs) gives the same image (hash of the frame) and the same render
//          observation (C3) as linear production.
// Arguments: core, ROM, take of 600 frames.
#include "audio_qa_inspection.h"
#include "content_hash.h"
#include "engine_game_state_restore.h"
#include "recording_header.h"
#include "recording_replay_preparation.h"
#include "recovery_planner.h"
#include "render_observation.h"

#include <ayther/ayther_session.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qa = ayther::audio_qa;
namespace ri = ayther::replay_inspection;
namespace rt = ayther::runtime;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

class Observer final : public ayther::engine::render_observation::RenderObserver {
  public:
    void on_render_frame(
        const ayther::engine::render_observation::RenderFrameView &frame) noexcept override {
        try {
            last = ri::render::copy_observation(frame);
        } catch (...) {
            last.reset();
        }
    }
    std::optional<ri::render::RenderObservation> last;
};

// The frame as the oracle compares it: the image and the copied observation.
struct Seen {
    std::string image;
    std::string observation;
    bool operator==(const Seen &) const = default;
};

std::string hex(const qa::ContentIdentity &identity) {
    std::string text;
    for (const auto byte : identity.sha256)
        text += std::to_string(byte) + '.';
    return text;
}

Seen see(const ayther::FrameView &view, const Observer &observer) {
    Seen seen;
    if (view.fb_pixels != nullptr)
        seen.image = hex(qa::identify_content(
            std::span{static_cast<const std::byte *>(view.fb_pixels),
                      static_cast<std::size_t>(view.fb_pitch) * view.fb_height}));
    if (observer.last) {
        const auto &o = *observer.last;
        seen.observation = std::to_string(o.emulation_frame) + "|" +
                           std::to_string(static_cast<int>(o.composability)) + "|" +
                           std::to_string(o.occurrences_total) + "|" +
                           std::to_string(o.replacements_total);
        for (const auto &occurrence : o.occurrences)
            seen.observation += "|" + std::to_string(occurrence.identity_hash) + ":" +
                                std::to_string(static_cast<int>(occurrence.status)) + ":" +
                                occurrence.pose.value;
        for (const auto &replacement : o.replacements)
            seen.observation +=
                "|" + replacement.asset + ":" + std::to_string(replacement.members.size());
    }
    return seen;
}

std::vector<std::byte> read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    const std::vector<char> characters{std::istreambuf_iterator<char>{input},
                                       std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(characters.size());
    std::memcpy(bytes.data(), characters.data(), characters.size());
    return bytes;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "usage: checkpoint_recovery_test <core> <rom> <take>\n";
        return 2;
    }
    const auto recording = read(argv[3]);
    const auto layout = qa::decode_recording_layout(recording);
    if (layout.error != qa::RecordingLayoutError::none || layout.layout.frame_count != 600U) {
        std::cerr << "the take of 600 frames is not readable\n";
        return 2;
    }
    const auto frames = layout.layout.frame_count;

    Observer observer;
    ayther::AytherSession::Config config;
    config.core_path = argv[1];
    config.rom_path = argv[2];
    config.enable_audio = false;
    config.derive_core_pack = false;
    config.render_observer = &observer;
    auto created = ayther::AytherSession::create(config);
    if (!created) {
        std::cerr << "the Engine session could not be created\n";
        return 2;
    }
    auto session = std::move(*created);
    const auto prepared = qa::prepare_recording_replay(recording, layout.layout, session.get(),
                                                       qa::restore_engine_game_state);
    if (!prepared.inputs || !prepared.restore.succeeded) {
        std::cerr << "the take could not be prepared\n";
        return 2;
    }
    std::vector<std::uint16_t> buttons;
    auto inputs = *prepared.inputs;
    while (const auto input = inputs.next())
        buttons.push_back(input->buttons);

    const auto step = [&](std::uint32_t frame) -> Seen {
        session->set_input(0, buttons[frame]);
        const auto &view = session->step();
        observer.last.reset();
        session->publish_render_observation(nullptr);
        return see(view, observer);
    };

    // BR-139: linear production with a checkpoint every K = 60 frames.
    rt::CheckpointStore store{60U, ri::checkpoint_budget_bytes};
    expect(store.capture(*session, ri::initial_checkpoint_frame),
           "RF-3.6: the initial state is kept");
    std::vector<Seen> linear;
    linear.reserve(frames);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        linear.push_back(step(frame));
        if (store.should_capture(frame))
            expect(store.capture(*session, frame),
                   "RF-3.6: the checkpoint of frame " + std::to_string(frame) + " is kept");
    }
    const auto &entries = store.ring().entries();
    bool every_k = entries.size() == 11U && entries.front().frame == ri::initial_checkpoint_frame;
    for (std::size_t index = 1; every_k && index < entries.size(); ++index)
        every_k = entries[index].frame == static_cast<std::int64_t>(index * 60U - 1U);
    expect(every_k, "plan §4.4: the initial state and frames 59, 119, …, 599");
    expect(store.ring().total_bytes() > 0U &&
               store.ring().total_bytes() <= ri::checkpoint_budget_bytes,
           "P-12: the checkpoints of the take fit 512 MiB");
    expect(linear.front().image != linear.back().image && !linear.front().observation.empty(),
           "the take draws different frames and publishes its observation");

    // BR-155: oracle O3 for every frame of the take.
    std::uint32_t image_mismatches{};
    std::uint32_t observation_mismatches{};
    std::uint32_t failed{};
    std::optional<std::uint32_t> first_bad;
    session->set_audio_output_mode(ayther::AudioOutputMode::silent);
    for (std::uint32_t target = 0; target < frames; ++target) {
        const auto checkpoint = store.ring().latest_at_most(static_cast<std::int64_t>(target) - 1);
        if (!checkpoint ||
            store.restore(*session, *checkpoint) != rt::CheckpointStore::RestoreResult::restored) {
            ++failed;
            first_bad = first_bad.value_or(target);
            continue;
        }
        Seen seen;
        for (auto frame = static_cast<std::uint32_t>(*checkpoint + 1); frame <= target; ++frame)
            seen = step(frame);
        if (seen.image != linear[target].image)
            ++image_mismatches;
        if (seen.observation != linear[target].observation)
            ++observation_mismatches;
        if (!(seen == linear[target]))
            first_bad = first_bad.value_or(target);
    }
    std::cout << "o3 frames=" << frames << " checkpoints=" << entries.size()
              << " bytes=" << store.ring().total_bytes() << " failed=" << failed
              << " image_mismatches=" << image_mismatches
              << " observation_mismatches=" << observation_mismatches
              << " first_bad=" << (first_bad ? std::to_string(*first_bad) : "none") << '\n';
    expect(failed == 0U, "RF-5.2: every frame of the take can be recovered");
    expect(image_mismatches == 0U, "RF-3.6: recovering k gives the image of linear production");
    expect(observation_mismatches == 0U,
           "RF-3.6: recovering k gives the observation of linear production");

    // BR-139: a budget for three checkpoints keeps the initial state and drops the oldest.
    const auto size = entries[1].size_bytes;
    rt::CheckpointStore small{60U, entries.front().size_bytes + 3U * size + size / 2U};
    expect(small.capture(*session, ri::initial_checkpoint_frame),
           "a small store keeps the initial");
    for (std::uint32_t frame = 59U; frame < frames; frame += 60U)
        (void)small.capture(*session, frame);
    const auto &kept = small.ring().entries();
    expect(kept.size() == 4U && kept.front().frame == ri::initial_checkpoint_frame &&
               kept.back().frame == 599,
           "P-12: over budget the oldest checkpoints go, never the initial one");

    session.reset();
    if (failures != 0)
        return 1;
    std::cout << "checkpoints and recovery reproduce linear production\n";
    return 0;
}
