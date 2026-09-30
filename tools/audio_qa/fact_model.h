#pragma once

#include "model_limits.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

enum class Availability { known, not_applicable, unknown };
template <class T> struct Field {
    Availability availability{Availability::unknown};
    std::optional<T> value;
    std::string reason_code;
    bool operator==(const Field &) const = default;
};

template <class T> [[nodiscard]] bool consistent_availability(const Field<T> &field) noexcept {
    if (field.reason_code.size() > max_identity_bytes) {
        return false;
    }
    switch (field.availability) {
    case Availability::known:
        return field.value.has_value();
    case Availability::not_applicable:
    case Availability::unknown:
        return !field.value;
    }
    return false;
}

struct FactId {
    std::string run_id;
    std::string producer_id;
    std::uint64_t producer_sequence{};
    bool operator==(const FactId &) const = default;
};

struct PreexistingContext {
    std::string context_id;
    bool operator==(const PreexistingContext &) const = default;
};
using Cause = std::variant<FactId, PreexistingContext>;

struct SharedStateOrder {
    std::string state_id;
    std::uint64_t sequence{};
    bool operator==(const SharedStateOrder &) const = default;
};

enum class FactFieldUnit {
    none,
    emulation_frame,
    sample_frame,
    bytes,
    count,
    linear_gain,
    frames_per_second,
    nanoseconds,
};

using FactFieldValue =
    std::variant<std::monostate, bool, std::uint64_t, std::int64_t, double, std::string, FactId>;

struct FactField {
    std::string name;
    Availability availability{Availability::unknown};
    FactFieldUnit unit{FactFieldUnit::none};
    FactFieldValue value;
    std::string unavailable_reason;
    bool operator==(const FactField &) const = default;
};

struct Fact {
    FactId id;
    std::string kind;
    Field<std::uint64_t> frame_index;
    std::vector<Cause> cause_ids;
    Field<std::string> decision_id;
    Field<std::string> assignment_id;
    Field<std::string> occurrence_id;
    Field<std::string> reason_code;
    // Arrival order is never a substitute for the producer's observed shared-state order.
    Field<std::vector<SharedStateOrder>> shared_state_order;
    // Complete producer payload. The normalized fields above remain indexed
    // shortcuts; they never replace the typed observation fields.
    std::vector<FactField> fields;
    bool operator==(const Fact &) const = default;
};

// Forward links are legal here; final referential integrity is checked after all batches arrive.
[[nodiscard]] bool well_formed(const Fact &fact) noexcept;

} // namespace ayther::audio_qa
