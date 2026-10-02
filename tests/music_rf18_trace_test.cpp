#include "music_rf18_trace.h"

int main() {
  runtime::MusicRf18Trace trace;
  runtime::MusicRf18TraceRecord record;
  record.generation = 2;
  record.occurrence = 3;
  record.appearance = 4;
  record.visit = 5;
  record.iteration = 6;
  record.signature_role = "evidence";
  record.candidates = {10, 11};
  record.choice = 11;
  record.choice_kind = "identified";
  record.entry_q = 1'200;
  record.output_authority = "hd_region";
  record.voice_count = 2;
  record.pause_causes = {"host_pause", "game_music_pause"};
  record.pending_event = 12;
  record.request_frame = 100;
  record.confirmation_frame = 104;
  record.output_boundary = 108;
  record.reason = "position_unconfirmed";
  record.producer = "music_position_recovery";

  if (!trace.publish(record) || trace.schema_version() != 1 || trace.records().size() != 1)
    return 1;
  const auto &stored = trace.records().front();
  if (stored.generation != 2 || stored.occurrence != 3 || stored.appearance != 4 ||
      stored.visit != 5 || stored.iteration != 6 || stored.candidates.size() != 2 ||
      stored.entry_q != 1'200 || stored.voice_count != 2 ||
      stored.pause_causes.size() != 2 || stored.pending_event != 12 ||
      stored.request_frame != 100 || stored.confirmation_frame != 104 ||
      stored.output_boundary != 108 || trace.policy_recalculations() != 0)
    return 2;
  record.reason = "invented_reason";
  if (trace.publish(record))
    return 3;
  return 0;
}
