#pragma once

#include <cstdint>
#include <vector>

namespace ayther::replay_inspection {

// Spec 002, plan §8 P-9 (RNF-2; DI-12): the offset between the image of k+1 and its audio after
// a resume. All times are milliseconds on one steady clock of the Runtime.
//
// - The image of k+1 is presented when the Runtime's present call for it returns
//   (`resumed_presented`, with the Engine frame index of k+1).
// - The audio of k+1 reaches the device when its first output sample is handed to the audio
//   device by the SDL postmix callback. The Engine fact `audio_frame_output_boundary` gives
//   that sample: `output_position` of the frame on the line `engine_main_output`, after rate
//   control, at `sample_rate`. A boundary emitted after lost boundaries (`valid = false`) is
//   not trusted.
// - Its handing time is the time of the postmix block of the same line that contains it, plus
//   its position in the block at the output rate.
//
// offset = audio time − presentation time; positive when the audio comes after the image.
struct ResumeMark {
    std::uint64_t frame_index{};
    double presented_ms{};
};

struct FrameBoundaryMark {
    std::uint64_t emulation_frame{};
    std::uint64_t output_position{};
    std::uint32_t sample_rate{};
    bool valid{};
};

struct PcmBlockMark {
    double at_ms{};
    std::uint64_t begin{};
    std::uint64_t end{};
    std::uint32_t sample_rate{};
};

struct ResumeOffset {
    bool measured{};
    std::uint64_t output_sample{};
    double audio_ms{};
    double offset_ms{};
};

[[nodiscard]] std::vector<ResumeOffset>
resume_offsets(const std::vector<ResumeMark> &resumes,
               const std::vector<FrameBoundaryMark> &boundaries,
               const std::vector<PcmBlockMark> &blocks);

} // namespace ayther::replay_inspection
