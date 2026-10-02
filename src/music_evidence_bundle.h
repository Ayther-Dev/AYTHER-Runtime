#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace runtime {

struct PlaybackEvidence {
  std::string run_id;
  std::vector<std::uint64_t> decision_ids;
  std::string pcm_run_id;
  std::uint64_t first_sample{};
  std::uint64_t last_sample{};
  std::uint64_t internal_alignment_error{};
};

struct AnalysisEvidence {
  std::string revision;
  std::string inputs;
  std::string parameters;
  std::string results;
  double metric{};
};

struct InvariantEvidence {
  std::string diagnostic;
  bool state_preserved{};
  std::string state_hash;
};

class MusicEvidenceBundle {
public:
  [[nodiscard]] bool add_playback(PlaybackEvidence evidence) {
    if (evidence.run_id.empty() || evidence.run_id != evidence.pcm_run_id ||
        evidence.decision_ids.empty() || evidence.first_sample > evidence.last_sample ||
        evidence.internal_alignment_error != 0)
      return false;
    playback_.push_back(std::move(evidence));
    return true;
  }
  [[nodiscard]] bool add_analysis(AnalysisEvidence evidence) {
    if (evidence.revision.empty() || evidence.inputs.empty() ||
        evidence.parameters.empty() || evidence.results.empty() ||
        evidence.metric < 0.0 || evidence.metric > 1.0)
      return false;
    analyses_.push_back(std::move(evidence));
    return true;
  }
  [[nodiscard]] bool add_invariant(InvariantEvidence evidence) {
    if (evidence.diagnostic.empty() || !evidence.state_preserved ||
        evidence.state_hash.empty())
      return false;
    invariants_.push_back(std::move(evidence));
    return true;
  }
  [[nodiscard]] bool mark_audio_not_applicable(std::string_view reason) {
    if (reason.empty())
      return false;
    audio_not_applicable_reason_.assign(reason);
    return true;
  }
  [[nodiscard]] constexpr std::uint64_t fabricated_audio_frames() const noexcept {
    return 0;
  }

private:
  std::vector<PlaybackEvidence> playback_;
  std::vector<AnalysisEvidence> analyses_;
  std::vector<InvariantEvidence> invariants_;
  std::string audio_not_applicable_reason_;
};

} // namespace runtime
