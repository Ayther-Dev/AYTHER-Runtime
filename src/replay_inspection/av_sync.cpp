#include "av_sync.h"

#include <algorithm>

namespace ayther::replay_inspection {

std::vector<ResumeOffset> resume_offsets(const std::vector<ResumeMark> &resumes,
                                         const std::vector<FrameBoundaryMark> &boundaries,
                                         const std::vector<PcmBlockMark> &blocks) {
    std::vector<ResumeOffset> offsets;
    offsets.reserve(resumes.size());
    for (const auto &resume : resumes) {
        ResumeOffset result;
        const auto boundary = std::find_if(boundaries.begin(), boundaries.end(),
                                           [&resume](const FrameBoundaryMark &mark) {
                                               return mark.emulation_frame == resume.frame_index;
                                           });
        if (boundary != boundaries.end() && boundary->valid && boundary->sample_rate != 0U) {
            const auto block =
                std::find_if(blocks.begin(), blocks.end(), [&boundary](const PcmBlockMark &mark) {
                    return mark.sample_rate == boundary->sample_rate &&
                           mark.begin <= boundary->output_position &&
                           boundary->output_position < mark.end;
                });
            if (block != blocks.end()) {
                result.measured = true;
                result.output_sample = boundary->output_position;
                result.audio_ms =
                    block->at_ms + static_cast<double>(boundary->output_position - block->begin) *
                                       1000.0 / static_cast<double>(block->sample_rate);
                result.offset_ms = result.audio_ms - resume.presented_ms;
            }
        }
        offsets.push_back(result);
    }
    return offsets;
}

} // namespace ayther::replay_inspection
