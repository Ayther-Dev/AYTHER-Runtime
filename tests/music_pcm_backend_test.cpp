#include "music_pcm_backend.h"

#include <array>

int main() {
  runtime::MusicPcmBackend backend;
  const std::array<float, 4> old_pcm{0.1F, 0.2F, 0.3F, 0.4F};
  const std::array<float, 2> new_pcm{0.8F, 0.9F};
  if (!backend.enqueue(7, old_pcm) || backend.queued_frames() != 4)
    return 1;
  if (!backend.stop_deliveries(100) || !backend.invalidate_and_drain(7) ||
      backend.queued_frames() != 0 || !backend.confirm_drained(104) ||
      !backend.publish_generation(8, 108) || !backend.enqueue(8, new_pcm))
    return 2;
  const auto output = backend.consume();
  if (output.generation != 8 || output.first_frame != 108 ||
      output.samples.size() != 2 || output.samples[0] != 0.8F)
    return 3;

  runtime::MusicPcmBackend no_ack;
  if (!no_ack.enqueue(20, old_pcm) || !no_ack.stop_deliveries(200) ||
      no_ack.confirm_drained(201) || no_ack.publish_generation(21, 202) ||
      !no_ack.rollback(20) || no_ack.active_generation() != 20)
    return 4;
  return 0;
}
