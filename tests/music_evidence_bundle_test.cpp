#include "music_evidence_bundle.h"

int main() {
  runtime::MusicEvidenceBundle bundle;
  runtime::PlaybackEvidence playback;
  playback.run_id = "run-1";
  playback.decision_ids = {1, 2};
  playback.pcm_run_id = "run-1";
  playback.first_sample = 100;
  playback.last_sample = 199;
  playback.internal_alignment_error = 0;
  if (!bundle.add_playback(playback))
    return 1;
  playback.pcm_run_id = "run-2";
  if (bundle.add_playback(playback))
    return 2;

  runtime::AnalysisEvidence analysis{"rev-7", "inputs-sha", "threshold=0.90",
                                     "result-sha", 0.995};
  if (!bundle.add_analysis(analysis))
    return 3;
  runtime::InvariantEvidence rejection{"transition_conflict", true, "state-sha"};
  if (!bundle.add_invariant(rejection))
    return 4;

  if (bundle.mark_audio_not_applicable("") ||
      !bundle.mark_audio_not_applicable("rejection_without_playback") ||
      bundle.fabricated_audio_frames() != 0)
    return 5;
  return 0;
}
