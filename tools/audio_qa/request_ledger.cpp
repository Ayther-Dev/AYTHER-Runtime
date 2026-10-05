#include "request_ledger.h"

#include "durable_file.h"
#include "long_path.h"
#include "model_toml.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <toml++/toml.hpp>
#include <utility>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

struct RequestLedgerFactory {
    static RequestLedger create(std::filesystem::path path, const std::uint64_t generation,
                                std::vector<RequestLedger::Entry> entries) {
        return RequestLedger(std::move(path), generation, std::move(entries));
    }
};

namespace {

struct DecodedLedger {
    std::uint64_t generation{};
    std::vector<RequestLedger::Entry> entries;
};

std::span<const std::byte> bytes(const std::string_view text) noexcept {
    return {reinterpret_cast<const std::byte *>(text.data()), text.size()};
}

std::optional<std::uint64_t> decimal(const std::string_view value) noexcept {
    if (value.empty() || value.front() == '+' || value.front() == '-') {
        return std::nullopt;
    }
    std::uint64_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return result;
}

bool same_content(const Request &left, const Request &right) noexcept {
    return left.request_id == right.request_id && left.session_id == right.session_id &&
           left.conditions_id == right.conditions_id && left.take_ids == right.take_ids;
}

bool valid_pair(const Request &request, const Run &run) noexcept {
    if (request.admission != Admission::accepted || run.request_id != request.request_id) {
        return false;
    }
    return std::holds_alternative<std::string>(to_toml(request)) &&
           std::holds_alternative<std::string>(to_toml(run));
}

std::variant<std::string, RequestLedgerError>
encode_ledger(const std::uint64_t generation, const std::vector<RequestLedger::Entry> &entries) {
    try {
        if (generation == 0 || entries.empty() || entries.size() > max_registered_requests) {
            return RequestLedgerError::malformed_document;
        }
        toml::array encoded_entries;
        for (const auto &entry : entries) {
            if (!valid_pair(entry.request, entry.run)) {
                return RequestLedgerError::malformed_document;
            }
            const auto request_document = to_toml(entry.request);
            const auto run_document = to_toml(entry.run);
            encoded_entries.push_back(
                toml::table{{"request_id", entry.request.request_id},
                            {"request_document", std::get<std::string>(request_document)},
                            {"run_document", std::get<std::string>(run_document)}});
        }
        const toml::table document{{"schema_version", 1},
                                   {"schema_minor", 0},
                                   {"kind", "request_ledger"},
                                   {"generation", std::to_string(generation)},
                                   {"entries", std::move(encoded_entries)}};
        std::ostringstream stream;
        stream << document;
        auto text = stream.str();
        if (text.empty() || text.size() > max_request_ledger_bytes) {
            return RequestLedgerError::document_too_large;
        }
        return text;
    } catch (...) {
        return RequestLedgerError::malformed_document;
    }
}

std::variant<DecodedLedger, RequestLedgerError> decode_ledger(const std::string_view text) {
    if (text.size() > max_request_ledger_bytes) {
        return RequestLedgerError::document_too_large;
    }
    try {
        const auto document = toml::parse(text);
        if (document["schema_version"].value<std::int64_t>() != 1 ||
            document["schema_minor"].value<std::int64_t>() != 0) {
            return RequestLedgerError::incompatible_version;
        }
        if (document.size() != 5 || document["kind"].value<std::string>() != "request_ledger") {
            return RequestLedgerError::malformed_document;
        }
        const auto generation_text = document["generation"].value<std::string>();
        const auto *entries = document["entries"].as_array();
        if (!generation_text || !entries || entries->empty() ||
            entries->size() > max_registered_requests) {
            return RequestLedgerError::malformed_document;
        }
        const auto generation = decimal(*generation_text);
        if (!generation || *generation == 0) {
            return RequestLedgerError::malformed_document;
        }
        DecodedLedger result{*generation, {}};
        result.entries.reserve(entries->size());
        for (const auto &node : *entries) {
            const auto *table = node.as_table();
            const auto request_id =
                table ? (*table)["request_id"].value<std::string>() : std::nullopt;
            const auto request_document =
                table ? (*table)["request_document"].value<std::string>() : std::nullopt;
            const auto run_document =
                table ? (*table)["run_document"].value<std::string>() : std::nullopt;
            if (!table || table->size() != 3 || !request_id || !request_document || !run_document) {
                return RequestLedgerError::malformed_document;
            }
            const auto decoded_request = request_from_toml(*request_document);
            const auto decoded_run = run_from_toml(*run_document);
            const auto *request = std::get_if<Request>(&decoded_request);
            const auto *run = std::get_if<Run>(&decoded_run);
            if (request == nullptr || run == nullptr || request->request_id != *request_id ||
                !valid_pair(*request, *run) ||
                std::any_of(result.entries.begin(), result.entries.end(),
                            [&](const RequestLedger::Entry &entry) {
                                return entry.request.request_id == request->request_id ||
                                       entry.run.run_id == run->run_id;
                            })) {
                return RequestLedgerError::malformed_document;
            }
            result.entries.push_back({*request, *run});
        }
        return result;
    } catch (const toml::parse_error &) {
        return RequestLedgerError::malformed_document;
    } catch (...) {
        return RequestLedgerError::malformed_document;
    }
}

bool valid_root(const std::filesystem::path &path) noexcept {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status);
}

} // namespace

RequestLedger::RequestLedger(std::filesystem::path path, const std::uint64_t generation,
                             std::vector<Entry> entries)
    : path_(std::move(path)), generation_(generation), entries_(std::move(entries)) {
    entries_.reserve(max_registered_requests);
}

std::optional<RequestLedgerError> RequestLedger::publish(std::vector<Entry> entries,
                                                         const std::uint64_t generation) noexcept {
    const auto encoded = encode_ledger(generation, entries);
    const auto *text = std::get_if<std::string>(&encoded);
    if (text == nullptr) {
        return std::get<RequestLedgerError>(encoded);
    }
    const auto published = publish_durable_file(path_, bytes(*text));
    if (std::get_if<DurablePublishedFile>(&published) == nullptr) {
        return RequestLedgerError::publish_failed;
    }
    entries_ = std::move(entries);
    generation_ = generation;
    return std::nullopt;
}

RequestLedgerRegistrationResult RequestLedger::register_request(Request request, Run run) noexcept {
    try {
        request.admission = Admission::accepted;
        if (!valid_pair(request, run)) {
            return RequestRegistrationResult{RequestRegistrationDecision::invalid, {}};
        }
        const auto existing =
            std::find_if(entries_.begin(), entries_.end(), [&](const Entry &entry) {
                return entry.request.request_id == request.request_id;
            });
        if (existing != entries_.end()) {
            return RequestRegistrationResult{same_content(existing->request, request)
                                                 ? RequestRegistrationDecision::known
                                                 : RequestRegistrationDecision::identity_conflict,
                                             existing->run};
        }
        if (entries_.size() >= max_registered_requests ||
            generation_ == (std::numeric_limits<std::uint64_t>::max)()) {
            return RequestRegistrationResult{RequestRegistrationDecision::capacity_exceeded, {}};
        }
        if (std::any_of(entries_.begin(), entries_.end(),
                        [&](const Entry &entry) { return entry.run.run_id == run.run_id; })) {
            return RequestRegistrationResult{RequestRegistrationDecision::invalid, {}};
        }
        auto candidate = entries_;
        candidate.push_back({std::move(request), std::move(run)});
        const auto published = publish(std::move(candidate), generation_ + 1);
        if (published) {
            return *published;
        }
        return RequestRegistrationResult{RequestRegistrationDecision::accepted,
                                         entries_.back().run};
    } catch (...) {
        return RequestLedgerError::io_error;
    }
}

RequestRegistrationResult RequestLedger::inspect_request(Request request, Run run) const noexcept {
    try {
        request.admission = Admission::accepted;
        if (!valid_pair(request, run))
            return {RequestRegistrationDecision::invalid, {}};
        const auto existing =
            std::find_if(entries_.begin(), entries_.end(), [&](const Entry &entry) {
                return entry.request.request_id == request.request_id;
            });
        if (existing != entries_.end()) {
            return {same_content(existing->request, request)
                        ? RequestRegistrationDecision::known
                        : RequestRegistrationDecision::identity_conflict,
                    existing->run};
        }
        if (entries_.size() >= max_registered_requests)
            return {RequestRegistrationDecision::capacity_exceeded, {}};
        if (std::any_of(entries_.begin(), entries_.end(),
                        [&](const Entry &entry) { return entry.run.run_id == run.run_id; }))
            return {RequestRegistrationDecision::invalid, {}};
        return {RequestRegistrationDecision::accepted, std::move(run)};
    } catch (...) {
        return {RequestRegistrationDecision::invalid, {}};
    }
}

RequestLedgerUpdateResult RequestLedger::update_run(const Run &run) noexcept {
    try {
        const auto existing =
            std::find_if(entries_.begin(), entries_.end(), [&](const Entry &entry) {
                return entry.run.run_id == run.run_id && entry.request.request_id == run.request_id;
            });
        if (existing == entries_.end() ||
            generation_ == (std::numeric_limits<std::uint64_t>::max)() ||
            !valid_pair(existing->request, run)) {
            return false;
        }
        if (existing->run == run) {
            return true;
        }
        auto candidate = entries_;
        candidate[static_cast<std::size_t>(existing - entries_.begin())].run = run;
        const auto published = publish(std::move(candidate), generation_ + 1);
        if (published) {
            return *published;
        }
        return true;
    } catch (...) {
        return RequestLedgerError::io_error;
    }
}

const std::filesystem::path &RequestLedger::path() const noexcept { return path_; }

std::uint64_t RequestLedger::generation() const noexcept { return generation_; }

std::size_t RequestLedger::size() const noexcept { return entries_.size(); }

std::optional<Run> RequestLedger::find_run(const std::string_view request_id) const noexcept {
    const auto existing = std::find_if(entries_.begin(), entries_.end(), [&](const Entry &entry) {
        return entry.request.request_id == request_id;
    });
    return existing == entries_.end() ? std::nullopt : std::optional<Run>{existing->run};
}

std::span<const RequestLedger::Entry> RequestLedger::entries() const noexcept { return entries_; }

RequestLedgerOpenResult open_request_ledger(const std::filesystem::path &output_root) noexcept {
    try {
        if (!valid_root(output_root)) {
            return RequestLedgerError::invalid_root;
        }
        const auto path = output_root / "request-ledger.toml";
        // D-12: read through the extended form of the path, which has no MAX_PATH limit.
        const auto native = long_path(path);
        std::error_code error;
        const bool exists = std::filesystem::exists(native, error);
        if (error) {
            return RequestLedgerError::io_error;
        }
        if (!exists) {
            return RequestLedgerFactory::create(path, 0, {});
        }
        error.clear();
        const auto status = std::filesystem::symlink_status(native, error);
        if (error || !std::filesystem::is_regular_file(status) ||
            std::filesystem::is_symlink(status)) {
            return RequestLedgerError::io_error;
        }
        const auto size = std::filesystem::file_size(native, error);
        if (error) {
            return RequestLedgerError::io_error;
        }
        if (size > max_request_ledger_bytes) {
            return RequestLedgerError::document_too_large;
        }
        std::ifstream input(native, std::ios::binary);
        if (!input) {
            return RequestLedgerError::io_error;
        }
        const std::string text{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
        if (input.bad() || text.size() != size) {
            return RequestLedgerError::io_error;
        }
        const auto decoded = decode_ledger(text);
        const auto *ledger = std::get_if<DecodedLedger>(&decoded);
        if (ledger == nullptr) {
            return std::get<RequestLedgerError>(decoded);
        }
        return RequestLedgerFactory::create(path, ledger->generation, ledger->entries);
    } catch (...) {
        return RequestLedgerError::io_error;
    }
}

} // namespace ayther::audio_qa
