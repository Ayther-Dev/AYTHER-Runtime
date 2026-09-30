#include "checkpoint_threshold.h"

#include <cstdint>

namespace ayther::audio_qa {
namespace {

bool interval_reached(const std::uint64_t now_ms, const std::uint64_t since_ms) noexcept {
    return now_ms >= since_ms && now_ms - since_ms >= checkpoint_interval_ms;
}

} // namespace

CheckpointThreshold::CheckpointThreshold(const std::uint64_t started_at_ms) noexcept
    : last_observed_at_ms_(started_at_ms) {}

bool CheckpointThreshold::record_received(const std::uint64_t total_bytes,
                                          const std::uint64_t now_ms) noexcept {
    if (total_bytes < received_bytes_ || now_ms < last_observed_at_ms_) {
        return false;
    }
    if (total_bytes > received_bytes_) {
        if (received_bytes_ == durable_bytes_) {
            pending_since_ms_ = now_ms;
        }
        if (publication_bytes_ && received_bytes_ <= *publication_bytes_ &&
            total_bytes > *publication_bytes_ && !post_request_pending_since_ms_) {
            post_request_pending_since_ms_ = now_ms;
        }
        received_bytes_ = total_bytes;
    }
    last_observed_at_ms_ = now_ms;
    return true;
}

CheckpointPublicationRequest
CheckpointThreshold::request_publication(const std::uint64_t now_ms) noexcept {
    if (now_ms < last_observed_at_ms_) {
        return {};
    }
    last_observed_at_ms_ = now_ms;

    if (publication_bytes_) {
        const auto waiting_bytes = received_bytes_ - *publication_bytes_;
        const bool time_threshold = post_request_pending_since_ms_ &&
                                    interval_reached(now_ms, *post_request_pending_since_ms_);
        const bool byte_threshold = waiting_bytes >= checkpoint_pending_byte_limit;
        return {time_threshold || byte_threshold ? CheckpointPublicationAction::saturated
                                                 : CheckpointPublicationAction::none,
                *publication_bytes_, waiting_bytes, time_threshold, byte_threshold};
    }

    const auto pending_bytes = received_bytes_ - durable_bytes_;
    if (pending_bytes == 0 || !pending_since_ms_) {
        return {};
    }
    const bool time_threshold = interval_reached(now_ms, *pending_since_ms_);
    const bool byte_threshold = pending_bytes >= checkpoint_pending_byte_limit;
    if (!time_threshold && !byte_threshold) {
        return {CheckpointPublicationAction::none, 0, pending_bytes, false, false};
    }
    publication_bytes_ = received_bytes_;
    post_request_pending_since_ms_.reset();
    return {CheckpointPublicationAction::publish, *publication_bytes_, pending_bytes,
            time_threshold, byte_threshold};
}

bool CheckpointThreshold::confirm_publication(const StoredCheckpoint &checkpoint,
                                              const std::uint64_t now_ms) noexcept {
    if (!publication_bytes_ || now_ms < last_observed_at_ms_ ||
        checkpoint.checkpoint().sequence <= last_checkpoint_sequence_) {
        return false;
    }
    durable_bytes_ = *publication_bytes_;
    last_checkpoint_sequence_ = checkpoint.checkpoint().sequence;
    publication_bytes_.reset();
    last_observed_at_ms_ = now_ms;
    if (received_bytes_ == durable_bytes_) {
        pending_since_ms_.reset();
    } else {
        pending_since_ms_ = post_request_pending_since_ms_.value_or(now_ms);
    }
    post_request_pending_since_ms_.reset();
    return true;
}

std::uint64_t CheckpointThreshold::received_bytes() const noexcept { return received_bytes_; }

std::uint64_t CheckpointThreshold::durable_bytes() const noexcept { return durable_bytes_; }

std::uint64_t CheckpointThreshold::last_checkpoint_sequence() const noexcept {
    return last_checkpoint_sequence_;
}

bool CheckpointThreshold::publication_pending() const noexcept {
    return publication_bytes_.has_value();
}

} // namespace ayther::audio_qa
