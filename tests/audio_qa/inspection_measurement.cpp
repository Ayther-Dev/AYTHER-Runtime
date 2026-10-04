// Spec 002, BR-156 (RNF-2; plan §8 P-2 to P-7 and P-9): measures the replay inspection on the
// reference equipment, with the visible Runtime and simulated events. The Runtime writes the
// time marks of its router and its presentation (AYTHER_QA_TIMING_LOG); this program drives the
// takes, reads the marks and compares each measure with its budget.
// Arguments: runtime, core, ROM, fixture generator, work directory, then optionally:
//   --take <file.ayr>          a real take (Toma 3) instead of a synthetic one of 600 frames
//   --pack <file.ay> --trust-registry <file.toml>  the pack of the campaign and its registry
//   --core-option k=v, --profile p, --output o, --shaders   conditions of the campaign
//   --zones a,b,c              the three zones of P-3 and P-4 (100,300,500 by default)
//   --fps <rate>               the frame rate of the take for the periods of P-2 and P-5
//   --no-audio-tap             the Runtime does not mark the audio output (AYTHER_QA_AUDIO_TAP=0):
//                              a control run for P-2 to P-7, without the P-9 resume offset
//   --dry-run                  prints the plan and measures nothing
#include "av_sync.h"
#include "recording_header.h"
#include "runtime_session_harness.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace hn = ayther::audio_qa::harness;
namespace fs = std::filesystem;

namespace {

struct Mark {
    std::vector<std::string> fields;
};

std::vector<Mark> read_marks(const fs::path &path) {
    std::vector<Mark> marks;
    std::ifstream input(path);
    for (std::string line; std::getline(input, line);) {
        Mark mark;
        std::stringstream stream{line};
        for (std::string field; std::getline(stream, field, ',');)
            mark.fields.push_back(field);
        if (!mark.fields.empty())
            marks.push_back(std::move(mark));
    }
    return marks;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty())
        return NAN;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1U;
    return values[std::min(index, values.size() - 1U)];
}

double maximum(const std::vector<double> &values) {
    return values.empty() ? NAN : *std::max_element(values.begin(), values.end());
}

// The latency from each key of `action` (in `phase`) to the next mark named `presented`.
std::vector<double> latencies(const std::vector<Mark> &marks, const std::string &action,
                              const std::string &phase, const std::string &presented) {
    std::vector<double> result;
    for (std::size_t index = 0; index < marks.size(); ++index) {
        const auto &mark = marks[index];
        if (mark.fields.size() < 4U || mark.fields[0] != "key" || mark.fields[2] != action ||
            mark.fields[3] != phase)
            continue;
        const double pressed = std::stod(mark.fields[1]);
        for (auto next = index + 1U; next < marks.size(); ++next) {
            if (marks[next].fields[0] == "key")
                break;
            if (marks[next].fields[0] == presented && marks[next].fields.size() >= 2U) {
                result.push_back(std::stod(marks[next].fields[1]) - pressed);
                break;
            }
        }
    }
    return result;
}

int failures = 0;

void report(const std::string &id, const std::string &what, const std::vector<double> &values,
            double p95, double limit, const std::string &unit) {
    const bool ok = !values.empty() && p95 <= limit;
    if (!ok)
        ++failures;
    std::cout << "| " << id << " | " << what << " | " << values.size() << " | " << p95 << ' '
              << unit << " | " << maximum(values) << ' ' << unit << " | ≤ " << limit << ' ' << unit
              << " | " << (ok ? "cumple" : "NO cumple") << " |\n";
}

} // namespace

std::uint32_t take_frames(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<char> characters{std::istreambuf_iterator<char>{input},
                                 std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(characters.size());
    for (std::size_t index = 0; index < characters.size(); ++index)
        bytes[index] = static_cast<std::byte>(characters[index]);
    const auto layout = ayther::audio_qa::decode_recording_layout(bytes);
    return layout.error == ayther::audio_qa::RecordingLayoutError::none ? layout.layout.frame_count
                                                                        : 0U;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        std::cerr
            << "usage: inspection_measurement <runtime> <core> <rom> <fixture> <work> "
               "[--take f] [--pack f --trust-registry f] [--core-option k=v] [--profile p] "
               "[--output o] [--shaders] [--zones a,b,c] [--fps r] [--no-audio-tap] [--dry-run]\n";
        return 2;
    }
    const fs::path work{argv[5]};
    fs::path real_take;
    std::vector<std::wstring> extra;
    std::vector<std::uint32_t> zones{100U, 300U, 500U};
    double fps = 60.0;
    bool dry_run{};
    bool audio_tap = true;
    bool pack{};
    bool trust{};
    std::size_t core_options{};
    for (int index = 6; index < argc; ++index) {
        const std::string name{argv[index]};
        const bool has_value = index + 1 < argc;
        if (name == "--dry-run") {
            dry_run = true;
        } else if (name == "--no-audio-tap") {
            audio_tap = false;
        } else if (name == "--shaders") {
            extra.emplace_back(L"--shaders");
        } else if (has_value && name == "--take") {
            real_take = argv[++index];
        } else if (has_value && name == "--fps") {
            fps = std::stod(argv[++index]);
        } else if (has_value && name == "--zones") {
            zones.clear();
            std::stringstream list{argv[++index]};
            for (std::string zone; std::getline(list, zone, ',');)
                zones.push_back(static_cast<std::uint32_t>(std::stoul(zone)));
        } else if (has_value &&
                   (name == "--pack" || name == "--trust-registry" || name == "--core-option" ||
                    name == "--profile" || name == "--output")) {
            pack = pack || name == "--pack";
            trust = trust || name == "--trust-registry";
            core_options += name == "--core-option" ? 1U : 0U;
            extra.push_back(fs::path{name}.wstring());
            extra.push_back(fs::path{argv[++index]}.wstring());
        } else {
            std::cerr << "unknown or incomplete option: " << name << '\n';
            return 2;
        }
    }
    fs::create_directories(work);
    auto take = real_take;
    if (take.empty()) {
        take = work / "take-600.arp";
        const auto command =
            "\"\"" + std::string{argv[4]} + "\" \"" + take.string() + "\" --frames 600\"";
        if (std::system(command.c_str()) != 0)
            return 2;
    }
    const auto frames = take_frames(take);
    if (frames == 0U || zones.size() != 3U ||
        std::any_of(zones.begin(), zones.end(),
                    [frames](std::uint32_t zone) { return zone < 1U || zone + 10U >= frames; }) ||
        fps <= 0.0) {
        std::cerr << "the take, the zones or the rate are not usable\n";
        return 2;
    }
    const double period_ms = 1000.0 / fps;
    std::cout << "plan take_frames=" << frames << " zones=" << zones[0] << ',' << zones[1] << ','
              << zones[2] << " fps=" << fps << " pack=" << (pack ? "yes" : "no")
              << " trust=" << (trust ? "yes" : "no") << " core_options=" << core_options
              << " audio_tap=" << (audio_tap ? "yes" : "no") << '\n';
    if (dry_run)
        return 0;
    hn::Options base;
    base.runtime = argv[1];
    base.core = argv[2];
    base.rom = argv[3];
    base.take = take;
    base.root = work;
    base.presentation = "visible";
    base.extra_arguments = extra;
    const auto measure = [&](const std::string &label, const std::string &script,
                             hn::CancelWhen cancel = hn::CancelWhen::never) {
        auto options = base;
        options.label = label;
        options.script = script;
        options.cancel_when = cancel;
        const auto log = work / (label + ".timing.csv");
        fs::remove(log);
        options.extra_environment.emplace_back(L"AYTHER_QA_TIMING_LOG", log.wstring());
        if (!audio_tap)
            options.extra_environment.emplace_back(L"AYTHER_QA_AUDIO_TAP", L"0");
        auto session = hn::run(options);
        return std::make_pair(std::move(session), read_marks(log));
    };

    // P-2 and P-5: thirty pauses and resumes along the take. A pause that does not come within
    // a second is asked for once more and reported (`retry=`), instead of leaving the script
    // waiting until the end of the take.
    const std::string retry = " retry=1000";
    std::string pauses;
    for (std::uint32_t cycle = 0; cycle < 30U; ++cycle) {
        const auto frame = 15U + cycle * 18U;
        pauses += "frame=" + std::to_string(frame) + " key space down\nafter=0 key space up\n" +
                  "paused=" + std::to_string(frame + 1U) + retry + " key other up\n" +
                  "after=150 key space down\nafter=0 key space up\n";
    }
    // A long take is closed once its measures are taken.
    pauses += "after=500 close\n";
    const auto [pause_session, pause_marks] = measure("pause-resume", pauses);
    // P-3 and P-4: ten steps forward and ten back in three zones of the take.
    std::string steps;
    for (const std::uint32_t zone : zones) {
        steps += "frame=" + std::to_string(zone - 1U) + " key space down\nafter=0 key space up\n";
        auto position = zone;
        steps += "paused=" + std::to_string(position) + retry + " key other up\n";
        for (int step = 0; step < 10; ++step, ++position)
            steps +=
                "paused=" + std::to_string(position) + " key right down\nafter=0 key right up\n";
        for (int step = 0; step < 10; ++step, --position)
            steps += "paused=" + std::to_string(position) + " key left down\nafter=0 key left up\n";
        steps += "paused=" + std::to_string(position) + " key space down\nafter=0 key space up\n";
    }
    steps += "after=500 close\n";
    const auto [step_session, step_marks] = measure("steps", steps);
    // P-7: the overlay visible and hidden along the whole take.
    const auto [visible_session, visible_marks] =
        measure("overlay-visible", "frame=0 key i down\nafter=0 key i up\nframe=600 close\n");
    const auto [hidden_session, hidden_marks] =
        measure("overlay-hidden", "frame=0 key other up\nframe=600 close\n");
    // P-6: five cancellations while playing.
    std::vector<double> cancels;
    for (int run = 0; run < 5; ++run) {
        auto [session, marks] =
            measure("cancel-" + std::to_string(run), {}, hn::CancelWhen::playing);
        if (session.cancel_to_terminal_ms > 0.0)
            cancels.push_back(session.cancel_to_terminal_ms);
    }

    std::string device;
    float refresh{};
    for (const auto &mark : pause_marks)
        if (mark.fields.size() >= 3U && mark.fields[0] == "device") {
            device = mark.fields[1];
            refresh = std::stof(mark.fields[2]);
        }
    std::vector<double> overlay_visible;
    std::vector<double> overlay_hidden;
    for (const auto &mark : visible_marks)
        if (mark.fields.size() >= 3U && mark.fields[0] == "overlay" && mark.fields[1] == "1")
            overlay_visible.push_back(std::stod(mark.fields[2]));
    for (const auto &mark : hidden_marks)
        if (mark.fields.size() >= 3U && mark.fields[0] == "overlay" && mark.fields[1] == "0")
            overlay_hidden.push_back(std::stod(mark.fields[2]));
    std::vector<double> drains;
    std::size_t drained{};
    for (const auto &mark : pause_marks)
        if (mark.fields.size() >= 4U && mark.fields[0] == "pause_drain") {
            drains.push_back(std::stod(mark.fields[1]));
            drained += mark.fields[2] == "0" && mark.fields[3] == "0" ? 1U : 0U;
        }

    const auto toggle = std::to_string(0); // KeyAction::toggle
    const auto right = std::to_string(2);  // KeyAction::right
    const auto left = std::to_string(1);   // KeyAction::left
    auto pause_latency = latencies(pause_marks, toggle, "playing", "paused_presented");
    auto resume_latency = latencies(pause_marks, toggle, "paused", "resumed_presented");
    auto forward_latency = latencies(step_marks, right, "paused", "recovered_presented");
    auto back_latency = latencies(step_marks, left, "paused", "recovered_presented");
    for (auto *values : {&pause_latency, &resume_latency})
        for (auto &value : *values)
            value /= period_ms;

    // BR-156: the pauses asked for again, and scripts abandoned after a second miss.
    for (const auto *marks : {&pause_marks, &step_marks}) {
        std::size_t retried{};
        std::size_t abandoned{};
        for (const auto &mark : *marks) {
            retried += !mark.fields.empty() && mark.fields[0] == "script_retry" ? 1U : 0U;
            abandoned += !mark.fields.empty() && mark.fields[0] == "script_abandoned" ? 1U : 0U;
        }
        std::cout << "script " << (marks == &pause_marks ? "pause-resume" : "steps")
                  << " retries=" << retried << " abandoned=" << abandoned << '\n';
        if (abandoned != 0U)
            ++failures;
    }
    std::cout << "device=" << device << " refresh_hz=" << refresh << " take_frames=" << frames
              << " fps=" << fps << '\n';
    std::cout << "| Id | Medida | n | p95 | máximo | Presupuesto | Resultado "
                 "|\n|---|---|---|---|---|---|---|\n";
    report("P-2", "Respuesta a la pausa (periodos)", pause_latency, maximum(pause_latency), 2.0,
           "periodos");
    report("P-3", "Avance de un frame en pausa", forward_latency, percentile(forward_latency, 0.95),
           100.0, "ms");
    report("P-4", "Retroceso de un frame (K = 60)", back_latency, percentile(back_latency, 0.95),
           500.0, "ms");
    report("P-4", "Cualquier recuperación", back_latency, maximum(back_latency), 5000.0, "ms");
    report("P-5", "Reanudación (periodos)", resume_latency, maximum(resume_latency), 2.0,
           "periodos");
    report("P-6", "Cancelación hasta el terminal", cancels, maximum(cancels), 2000.0, "ms");
    report("P-7", "Coste de la depuración visible", overlay_visible,
           percentile(overlay_visible, 0.95), 1.0, "ms");
    report("P-7", "Coste de la depuración oculta", overlay_hidden, percentile(overlay_hidden, 0.95),
           0.05, "ms");
    std::cout << "| P-9 | Pausas drenadas por completo (código drained, 0 frames pendientes) | "
              << drains.size() << " | " << drained << " de " << drains.size() << " | drenaje máx. "
              << maximum(drains) << " ms | audio parado en el final de k | "
              << (drained == drains.size() && !drains.empty() ? "cumple" : "NO cumple") << " |\n";
    if (drained != drains.size() || drains.empty())
        ++failures;

    // P-9, resume: the first audio of k+1 handed to the device against the image of k+1
    // (definition in av_sync.h and evidence/perf.md).
    std::vector<ayther::replay_inspection::ResumeMark> resumes;
    std::vector<ayther::replay_inspection::FrameBoundaryMark> boundaries;
    std::vector<ayther::replay_inspection::PcmBlockMark> blocks;
    std::size_t lost_boundaries = 0;
    for (const auto &mark : pause_marks) {
        const auto &f = mark.fields;
        if (f.size() >= 4U && f[0] == "resumed_presented")
            resumes.push_back({std::stoull(f[3]), std::stod(f[1])});
        else if (f.size() >= 6U && f[0] == "frame_boundary") {
            boundaries.push_back({std::stoull(f[2]), std::stoull(f[3]),
                                  static_cast<std::uint32_t>(std::stoul(f[4])), f[5] == "1"});
            lost_boundaries += f[5] == "1" ? 0U : 1U;
        } else if (f.size() >= 5U && f[0] == "pcm_block")
            blocks.push_back({std::stod(f[1]), std::stoull(f[2]), std::stoull(f[3]),
                              static_cast<std::uint32_t>(std::stoul(f[4]))});
    }
    const auto offsets = ayther::replay_inspection::resume_offsets(resumes, boundaries, blocks);
    std::vector<double> magnitudes;
    std::vector<double> signed_offsets;
    for (const auto &offset : offsets)
        if (offset.measured) {
            magnitudes.push_back(std::abs(offset.offset_ms));
            signed_offsets.push_back(offset.offset_ms);
        }
    if (!audio_tap) {
        std::cout << "| P-9 | Desfase imagen–audio de k+1 al reanudar | — | — | — | — | sin medir "
                     "(--no-audio-tap) |\n";
        return failures == 0 ? 0 : 1;
    }
    const bool sync_ok = magnitudes.size() >= 30U && magnitudes.size() == offsets.size() &&
                         maximum(magnitudes) <= period_ms;
    if (!sync_ok)
        ++failures;
    std::cout << "| P-9 | Desfase imagen–audio de k+1 al reanudar (|audio − imagen|) | "
              << magnitudes.size() << " de " << offsets.size() << " | "
              << percentile(magnitudes, 0.95) << " ms | " << maximum(magnitudes)
              << " ms | ≤ 1 periodo (" << period_ms << " ms) | "
              << (sync_ok ? "cumple" : "NO cumple") << " |\n";
    std::cout << "p9 signed offsets ms (audio − image):";
    for (const auto value : signed_offsets)
        std::cout << ' ' << value;
    std::cout << "\np9 boundaries=" << boundaries.size() << " invalid=" << lost_boundaries
              << " blocks=" << blocks.size() << '\n';
    return failures == 0 ? 0 : 1;
}
