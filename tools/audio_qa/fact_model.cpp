#include "fact_model.h"
#include "model_limits.h"

#include <cmath>
#include <string_view>

namespace ayther::audio_qa {
namespace {
bool identifier(std::string_view value) noexcept {
    return !value.empty() && value.size() <= max_identity_bytes;
}
template <class T> bool valid_field(const Field<T> &field) noexcept {
    return consistent_availability(field);
}
bool valid_field(const Field<std::string> &field) noexcept {
    return valid_field<std::string>(field) && (!field.value || identifier(*field.value));
}
bool valid_id(const FactId &id) noexcept {
    return identifier(id.run_id) && identifier(id.producer_id) && id.producer_sequence != 0;
}
bool valid_observation_field(const FactField &field, const std::string_view run_id) noexcept {
    if (!identifier(field.name) || field.unavailable_reason.size() > max_fact_field_text_bytes)
        return false;
    if (static_cast<std::size_t>(field.unit) > static_cast<std::size_t>(FactFieldUnit::nanoseconds))
        return false;
    const bool has_value = !std::holds_alternative<std::monostate>(field.value);
    if ((field.availability == Availability::known) != has_value)
        return false;
    if (field.availability == Availability::known && !field.unavailable_reason.empty())
        return false;
    if (field.availability != Availability::known && field.unavailable_reason.empty())
        return false;
    if (const auto *text = std::get_if<std::string>(&field.value))
        return text->size() <= max_fact_field_text_bytes;
    if (const auto *value = std::get_if<double>(&field.value))
        return std::isfinite(*value);
    if (const auto *id = std::get_if<FactId>(&field.value))
        return valid_id(*id) && id->run_id == run_id;
    return true;
}
} // namespace

bool well_formed(const Fact &fact) noexcept {
    if (!valid_id(fact.id) || !identifier(fact.kind) || !valid_field(fact.frame_index) ||
        !valid_field(fact.decision_id) || !valid_field(fact.assignment_id) ||
        !valid_field(fact.occurrence_id) || !valid_field(fact.reason_code) ||
        !valid_field(fact.shared_state_order) || fact.cause_ids.size() > max_fact_causes ||
        fact.fields.size() > max_fact_fields) {
        return false;
    }
    for (std::size_t i = 0; i < fact.fields.size(); ++i) {
        if (!valid_observation_field(fact.fields[i], fact.id.run_id))
            return false;
        for (std::size_t previous = 0; previous < i; ++previous)
            if (fact.fields[previous].name == fact.fields[i].name)
                return false;
    }
    for (std::size_t i = 0; i < fact.cause_ids.size(); ++i) {
        const auto &cause = fact.cause_ids[i];
        if (const auto *internal = std::get_if<FactId>(&cause)) {
            if (!valid_id(*internal) || internal->run_id != fact.id.run_id ||
                *internal == fact.id) {
                return false;
            }
        } else if (!identifier(std::get<PreexistingContext>(cause).context_id)) {
            return false;
        }
        for (std::size_t previous = 0; previous < i; ++previous) {
            if (fact.cause_ids[previous] == cause) {
                return false;
            }
        }
    }
    if (fact.shared_state_order.value) {
        const auto &orders = *fact.shared_state_order.value;
        if (orders.empty() || orders.size() > max_fact_state_orders) {
            return false;
        }
        for (std::size_t i = 0; i < orders.size(); ++i) {
            if (!identifier(orders[i].state_id) || orders[i].sequence == 0) {
                return false;
            }
            for (std::size_t previous = 0; previous < i; ++previous) {
                if (orders[previous].state_id == orders[i].state_id) {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace ayther::audio_qa
