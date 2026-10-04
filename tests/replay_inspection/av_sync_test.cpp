// Spec 002, BR-156 (RNF-2; plan §8 P-9; DI-12): the offset between the image of k+1 and its
// audio after a resume. The Engine places the start of each frame on the device output line
// (`audio_frame_output_boundary`); the Runtime times the postmix blocks of that same line.
// Synthetic marks, without a device.
#include "av_sync.h"

#include <cmath>
#include <iostream>
#include <string_view>

namespace ri = ayther::replay_inspection;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

bool near(double value, double expected) { return std::abs(value - expected) < 1e-6; }

} // namespace

int main() {
    // Device line at 48 kHz. Frame 101 (k+1 of the first resume) starts at output sample 52800.
    // Boundaries were lost before frame 301: its boundary is not trusted.
    const std::vector<ri::FrameBoundaryMark> boundaries{
        {100, 51000, 48000, true}, {101, 52800, 48000, true},  {102, 53600, 48000, true},
        {201, 70000, 48000, true}, {301, 90000, 48000, false},
    };
    const std::vector<ri::PcmBlockMark> blocks{
        {1240.0, 50400, 52800, 48000},
        {1290.0, 52800, 55200, 48000}, // contains 52800, handed at 1290 → exactly 1290
        {2120.0, 69600, 72000, 48000}, // contains 70000 → 2120 + 400/48 = 2128.333…
        {3100.0, 88800, 91200, 48000},
    };
    const std::vector<ri::ResumeMark> resumes{
        {101, 1210.0}, // k+1 is engine frame 101, presented at 1210
        {201, 2112.0},
        {301, 3090.0}, // its boundary is not valid
        {401, 4000.0}, // no boundary at all
    };
    const auto offsets = ri::resume_offsets(resumes, boundaries, blocks);
    expect(offsets.size() == 4U, "one result per resume");
    expect(offsets[0].measured && near(offsets[0].audio_ms, 1290.0) &&
               near(offsets[0].offset_ms, 80.0) && offsets[0].output_sample == 52800U,
           "P-9: the first device sample of k+1, against the image of k+1");
    expect(offsets[1].measured && near(offsets[1].audio_ms, 2120.0 + 400.0 / 48.0) &&
               near(offsets[1].offset_ms, 2120.0 + 400.0 / 48.0 - 2112.0),
           "P-9: the sample inside its block is placed by its position and the rate");
    expect(!offsets[2].measured, "a boundary after lost boundaries is not trusted");
    expect(!offsets[3].measured, "a resume without its boundary is reported, not invented");
    const auto unseen = ri::resume_offsets({{101, 1210.0}}, boundaries, {});
    expect(unseen.size() == 1U && !unseen[0].measured, "without the PCM block, no offset");
    if (failures != 0)
        return 1;
    std::cout << "the resume offset is taken from the frame boundary on the device line\n";
    return 0;
}
