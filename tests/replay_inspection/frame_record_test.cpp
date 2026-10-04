// Spec 002 (plan §4.3, §5.9; contracts.md C3; §8 P-11): the per-frame debug record.
//   BR-131 (RF-7.2, RF-7.3, RF-7.7, RF-7.8, RF-7.9, RNF-3) occurrences, replacements,
//          omission of what is not known, empty frames, reasons and the P-11 overflow.
//   BR-132 (RF-7.4, RF-7.5) processing time and instantaneous FPS.
#include "frame_record.h"

#include <iostream>
#include <string>
#include <string_view>

namespace ri = ayther::replay_inspection;
namespace ro = ayther::replay_inspection::render;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

ro::TextField known(std::string value) { return {ro::Availability::known, std::move(value)}; }
ro::TextField unknown() { return {ro::Availability::unknown, {}}; }
ro::TextField not_applicable() { return {ro::Availability::not_applicable, {}}; }

ro::ObservedOccurrence occurrence(std::uint16_t index, std::uint8_t slot, std::uint64_t hash,
                                  ro::OccurrenceStatus status, std::int32_t replacement,
                                  ro::TextField pose, ro::TextField reason) {
    return {{index, slot, 0}, hash, status, replacement, std::move(pose), std::move(reason)};
}

ri::FrameGeneral general() { return {"Golden Axe.md", "Toma 3.ayr", std::nullopt, "paused", 7892}; }

void occurrences_and_replacements() {
    ro::RenderObservation observation;
    observation.emulation_frame = 4311;
    observation.occurrences = {
        occurrence(0, 3, 0xaa, ro::OccurrenceStatus::replaced, 0, known("ax_walk_2"),
                   not_applicable()),
        occurrence(1, 7, 0xaa, ro::OccurrenceStatus::replaced, 0, known("ax_walk_2"),
                   not_applicable()),
        occurrence(2, 9, 0xbb, ro::OccurrenceStatus::original_unassigned, -1, not_applicable(),
                   not_applicable()),
        occurrence(3, 11, 0xcc, ro::OccurrenceStatus::assigned_not_applied, -1, known("enemy_1"),
                   known("texture_pending")),
        occurrence(4, 12, 0xdd, ro::OccurrenceStatus::assigned_not_applied, -1, unknown(),
                   unknown()),
    };
    observation.replacements = {{0,
                                 "pose",
                                 "ax_walk_2",
                                 "poses/ax_walk_2.png",
                                 {0, 1},
                                 ro::Availability::known,
                                 ro::DrawOutcome::partitioned,
                                 ro::TextureState::ready}};
    observation.occurrences_total = observation.occurrences.size();
    observation.replacements_total = observation.replacements.size();
    const auto record = ri::make_frame_record(4310, 2, general(), observation, {});
    expect(record.frame == 4310U && record.visit == 2U && record.rows.size() == 5U,
           "RF-7.1: the record is of frame k and its visit");
    expect(record.rows[0].identity == record.rows[1].identity &&
               record.rows[0].slot != record.rows[1].slot,
           "RF-7.2: two occurrences of the same identity are distinguished");
    expect(record.rows[0].pose == "ax_walk_2" && record.rows[0].asset == "poses/ax_walk_2.png" &&
               record.rows[0].draw == ro::DrawOutcome::partitioned,
           "RF-7.3: a replacement shows its identity, pose and asset of the same frame");
    expect(!record.rows[2].pose && !record.rows[2].asset && !record.rows[2].reason,
           "RF-7.7: what is not known is omitted");
    expect(record.rows[3].reason == "texture_pending" && record.rows[3].pose == "enemy_1",
           "RF-7.9: an assignment not applied shows its known reason");
    expect(!record.rows[4].reason && !record.rows[4].pose,
           "RF-7.9: without a known reason no reason is shown");
    expect(!record.overflow && record.general.pack_label() == "Sin pack",
           "RF-1.3: without pack the record says «Sin pack»");
}

void empty_frame_and_overflow() {
    ro::RenderObservation empty;
    const auto record = ri::make_frame_record(10, 1, general(), empty, {});
    expect(record.rows.empty() && !record.overflow,
           "RF-7.8: a frame without identities has an empty list, nothing of the previous one");
    ro::RenderObservation crowded;
    for (std::uint16_t index = 0; index < 257; ++index)
        crowded.occurrences.push_back(occurrence(index, static_cast<std::uint8_t>(index % 80),
                                                 index, ro::OccurrenceStatus::original_unassigned,
                                                 -1, not_applicable(), not_applicable()));
    crowded.occurrences_total = 300;
    const auto over = ri::make_frame_record(11, 1, general(), crowded, {});
    expect(over.rows.size() == ri::max_record_rows && over.overflow &&
               over.overflow->rows_total == 300U && over.overflow->limit == 256U,
           "P-11: above 256 rows the excess is stated with its total, never truncated silently");
}

void measurements() {
    ri::FrameMeasurer measurer;
    const auto first = measurer.measure({0, 0.0, 9.0, 16.0}, ri::FrameContext::continuous);
    expect(first.processing_ms == 9.0 && !first.fps_instant,
           "RF-7.5: the first frame has no instantaneous FPS");
    const auto second = measurer.measure({1, 16.0, 26.0, 33.0}, ri::FrameContext::continuous);
    expect(second.processing_ms == 10.0 && second.fps_instant &&
               *second.fps_instant == 1000.0 / 17.0,
           "RF-7.4, RF-7.5: consecutive presentations give the FPS of that interval");
    const auto third = measurer.measure({2, 33.0, 40.0, 58.0}, ri::FrameContext::continuous);
    expect(third.fps_instant && *third.fps_instant == 1000.0 / 25.0,
           "RF-7.4: the FPS is never averaged with earlier intervals");
    const auto resumed = measurer.measure({3, 900.0, 905.0, 916.0}, ri::FrameContext::after_pause);
    expect(resumed.processing_ms == 5.0 && !resumed.fps_instant,
           "RF-7.5: after a pause there is no valid interval");
    const auto visit = measurer.measure({2, 950.0, 956.0, 960.0}, ri::FrameContext::navigation);
    expect(visit.processing_ms == 6.0 && !visit.fps_instant,
           "RF-7.5: a navigation visit measures only the target frame and has no FPS");
    const auto after_visit =
        measurer.measure({3, 1000.0, 1003.0, 1010.0}, ri::FrameContext::continuous);
    expect(!after_visit.fps_instant,
           "RF-7.5: a presentation after a visit is not consecutive in continuous playback");
}

} // namespace

int main() {
    occurrences_and_replacements();
    empty_frame_and_overflow();
    measurements();
    if (failures != 0)
        return 1;
    std::cout << "debug records follow plan §4.3 and §5.9\n";
    return 0;
}
