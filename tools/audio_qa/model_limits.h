#pragma once

#include <cstddef>

namespace ayther::audio_qa {
inline constexpr std::size_t max_metadata_bytes = 256 * 1024;
inline constexpr std::size_t max_identity_bytes = 256;
inline constexpr std::size_t max_request_takes = 1024;
inline constexpr std::size_t max_reference_materials = 4096;
inline constexpr std::size_t max_reference_conditions = 256;
inline constexpr std::size_t max_reference_differences = 256;
inline constexpr std::size_t max_reference_value_bytes = 4096;
inline constexpr std::size_t max_fact_causes = 256;
inline constexpr std::size_t max_fact_state_orders = 256;
inline constexpr std::size_t max_fact_fields = 128;
inline constexpr std::size_t max_fact_field_text_bytes = 4096;
inline constexpr std::size_t max_mix_participants = 256;
inline constexpr std::size_t max_audio_payload_bytes = 256 * 1024;
inline constexpr std::size_t max_audio_discontinuities = 256;
inline constexpr std::size_t max_initial_state_bytes = 64 * 1024 * 1024;
inline constexpr std::size_t max_initial_windows = 4096;
inline constexpr std::size_t max_initial_requests = 4096;
inline constexpr std::size_t max_initial_audio_queues = 256;
inline constexpr std::size_t max_capability_names = 64;
inline constexpr std::size_t max_contract_versions = 16;
} // namespace ayther::audio_qa
