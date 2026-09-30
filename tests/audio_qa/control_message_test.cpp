#include "control_message.h"

#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {
void require(const bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        const qa::Request pending{"request-91",
                                  "session-1",
                                  "conditions-a",
                                  {"take-main", "take-extra"},
                                  qa::Admission::pending};
        const auto encoded_request = qa::encode_request_message(pending, 17);
        const auto *request_bytes = std::get_if<std::vector<std::byte>>(&encoded_request);
        require(request_bytes != nullptr, "request_encoding_failed");
        const auto decoded_request = qa::decode_request_message(*request_bytes, 17);
        require(std::holds_alternative<qa::Request>(decoded_request) &&
                    std::get<qa::Request>(decoded_request) == pending,
                "request_round_trip_lost_fields");

        auto answered = std::get<qa::Request>(decoded_request);
        for (const auto admission : {qa::Admission::accepted, qa::Admission::rejected}) {
            answered.admission = admission;
            const auto encoded_response = qa::encode_admission_message(answered, 23);
            const auto *response_bytes = std::get_if<std::vector<std::byte>>(&encoded_response);
            require(response_bytes != nullptr, "response_encoding_failed");
            const auto decoded_response = qa::decode_admission_message(*response_bytes, 23);
            require(std::holds_alternative<qa::Request>(decoded_response) &&
                        std::get<qa::Request>(decoded_response) == answered,
                    "response_round_trip_lost_identity");
        }

        require(std::holds_alternative<qa::ControlMessageError>(
                    qa::decode_request_message(*request_bytes, 18)),
                "sequence_mismatch_accepted");
        auto truncated = *request_bytes;
        truncated.pop_back();
        require(std::holds_alternative<qa::ControlMessageError>(
                    qa::decode_request_message(truncated, 17)),
                "truncated_payload_accepted");
        require(std::holds_alternative<qa::ControlMessageError>(
                    qa::decode_admission_message(*request_bytes, 17)),
                "request_accepted_as_response");
        answered.admission = qa::Admission::accepted;
        require(std::holds_alternative<qa::ControlMessageError>(
                    qa::encode_request_message(answered, 1)),
                "accepted_request_encoded_as_new_request");
        require(std::holds_alternative<qa::ControlMessageError>(
                    qa::encode_admission_message(pending, 1)),
                "pending_response_encoded");

        std::puts("control_message_test: passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "control_message_test: %s\n", error.what());
        return 1;
    }
}
