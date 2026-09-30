#include "fact_model.h"
#include "model_limits.h"

#include <cstdio>
#include <stdexcept>

namespace qa = ayther::audio_qa;
namespace {
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        qa::Fact fact;
        fact.id = {"run-1", "mixer", 3};
        fact.kind = "voice_replaced";
        fact.frame_index = {qa::Availability::known, 0, {}};
        fact.cause_ids = {qa::FactId{"run-1", "detector", 7}, qa::FactId{"run-1", "session", 12},
                          qa::PreexistingContext{"initial-o1"}};
        fact.shared_state_order = {
            qa::Availability::known, std::vector<qa::SharedStateOrder>{{"voices", 5}}, {}};
        require(qa::well_formed(fact), "multiple_causes_or_forward_link_rejected");
        auto second_effect = fact;
        second_effect.id.producer_sequence = 4;
        second_effect.kind = "voice_started";
        require(qa::well_formed(second_effect) && second_effect.cause_ids == fact.cause_ids,
                "shared_causes_for_two_effects_lost");
        fact.cause_ids.push_back(fact.cause_ids[0]);
        require(!qa::well_formed(fact), "duplicate_cause_accepted");
        fact.cause_ids.back() = qa::FactId{"run-2", "detector", 7};
        require(!qa::well_formed(fact), "cross_run_link_accepted");
        fact.cause_ids.back() = fact.id;
        require(!qa::well_formed(fact), "self_cause_accepted");
        fact.cause_ids.pop_back();
        fact.shared_state_order = {qa::Availability::not_applicable, std::nullopt, "independent"};
        require(qa::well_formed(fact), "independent_order_rejected");
        fact.shared_state_order = {qa::Availability::unknown, std::nullopt, "not_observed"};
        require(qa::well_formed(fact), "unknown_order_diagnostic_rejected");
        fact.frame_index.availability = qa::Availability::unknown;
        require(!qa::well_formed(fact), "unknown_field_with_value_accepted");
        fact.frame_index.value.reset();
        fact.cause_ids.clear();
        for (std::size_t i = 0; i < qa::max_fact_causes; ++i) {
            fact.cause_ids.push_back(qa::FactId{"run-1", "detector", i + 1});
        }
        require(qa::well_formed(fact), "cause_limit_boundary_rejected");
        fact.cause_ids.push_back(qa::PreexistingContext{"extra"});
        require(!qa::well_formed(fact), "cause_limit_ignored");
        fact.cause_ids.pop_back();
        fact.shared_state_order = {
            qa::Availability::known, std::vector<qa::SharedStateOrder>{}, {}};
        for (std::size_t i = 0; i < qa::max_fact_state_orders; ++i) {
            fact.shared_state_order.value->push_back({"state-" + std::to_string(i), 1});
        }
        require(qa::well_formed(fact), "state_order_limit_boundary_rejected");
        fact.shared_state_order.value->push_back({"extra", 1});
        require(!qa::well_formed(fact), "state_order_limit_ignored");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "qa_fact_model_test_failed: %s\n", error.what());
        return 1;
    }
}
