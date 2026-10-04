#pragma once

#include "check_summary.h"
#include "durable_file.h"
#include "effective_values.h"
#include "material_preflight.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::int64_t request_summary_schema_version = 1;
inline constexpr std::int64_t request_summary_schema_minor = 1;

// Spec 002 (contracts.md C2, plan §4.6; RF-2.3, RF-2.4, RF-2.12, RNF-5): the confirmed
// summary of a request: its selections with their origin and SHA-256, the identity of its
// conditions, the result of each take with playback, traversal and evidence apart, and
// the joint result. It is written durably when the result is confirmed, and a repeated
// request returns it as it is, without starting the Runtime.
struct StoredTakeSummary {
    std::size_t position{};
    std::string take;
    std::string sha256;
    // Empty for a take that did not start.
    std::string run_id;
    std::string outcome;
    std::string diagnostic;
    std::string playback;
    std::string traversal;
    std::string evidence;
    std::vector<std::string> evidence_reasons;
    bool operator==(const StoredTakeSummary &) const = default;
};

struct StoredMaterial {
    std::string role;
    std::string field;
    std::string path;
    std::string sha256;
    std::uint64_t size{};
    bool operator==(const StoredMaterial &) const = default;
};

struct RequestSummaryDocument {
    std::string request_id;
    std::string conditions_id;
    std::string run_id;
    bool confirmed{true};
    int exit_code{};
    bool linear_complete{};
    std::vector<EffectiveValue> values;
    std::vector<StoredMaterial> materials;
    std::vector<StoredTakeSummary> takes;
    bool operator==(const RequestSummaryDocument &) const = default;
};

// `<output>/requests/<run_id>/request-summary.toml`: the run id of the request is a
// generated identifier, safe as a directory name, which the ledger keeps for the request.
[[nodiscard]] std::filesystem::path request_summary_path(const std::filesystem::path &output,
                                                         std::string_view run_id);

[[nodiscard]] StoredMaterial stored_material(const MaterialPin &pin);
[[nodiscard]] std::string format_request_summary(const RequestSummaryDocument &document);

enum class RequestSummaryReadError { missing, unreadable, unsupported_version, invalid };

using RequestSummaryReadResult = std::variant<RequestSummaryDocument, RequestSummaryReadError>;

// A major version other than 1 is rejected and nothing is modified (RNF-5).
[[nodiscard]] RequestSummaryReadResult parse_request_summary(std::string_view text);
[[nodiscard]] RequestSummaryReadResult read_request_summary(const std::filesystem::path &path);
[[nodiscard]] DurablePublishResult write_request_summary(const std::filesystem::path &path,
                                                         const RequestSummaryDocument &document,
                                                         DurablePublicationLimits limits = {});

// The technical summary the console prints, rebuilt from the stored document.
[[nodiscard]] std::optional<CheckTechnicalSummary>
technical_summary(const RequestSummaryDocument &document);

} // namespace ayther::audio_qa
