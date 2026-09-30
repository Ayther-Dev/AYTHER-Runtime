#pragma once

#include "request_registry.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace ayther::audio_qa {

inline constexpr std::uint64_t max_request_ledger_bytes = 64U * 1024U * 1024U;

enum class RequestLedgerError {
    invalid_root,
    document_too_large,
    malformed_document,
    incompatible_version,
    publish_failed,
    io_error,
};

using RequestLedgerRegistrationResult = std::variant<RequestRegistrationResult, RequestLedgerError>;
using RequestLedgerUpdateResult = std::variant<bool, RequestLedgerError>;

class RequestLedger final {
  public:
    struct Entry {
        Request request;
        Run run;
    };

    [[nodiscard]] RequestLedgerRegistrationResult register_request(Request request,
                                                                   Run run) noexcept;
    [[nodiscard]] RequestRegistrationResult inspect_request(Request request,
                                                            Run run) const noexcept;
    [[nodiscard]] RequestLedgerUpdateResult update_run(const Run &run) noexcept;

    [[nodiscard]] const std::filesystem::path &path() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::optional<Run> find_run(std::string_view request_id) const noexcept;
    [[nodiscard]] std::span<const Entry> entries() const noexcept;

  private:
    RequestLedger(std::filesystem::path path, std::uint64_t generation, std::vector<Entry> entries);
    [[nodiscard]] std::optional<RequestLedgerError> publish(std::vector<Entry> entries,
                                                            std::uint64_t generation) noexcept;

    std::filesystem::path path_;
    std::uint64_t generation_{};
    std::vector<Entry> entries_;

    friend struct RequestLedgerFactory;
};

using RequestLedgerOpenResult = std::variant<RequestLedger, RequestLedgerError>;

[[nodiscard]] RequestLedgerOpenResult
open_request_ledger(const std::filesystem::path &output_root) noexcept;

} // namespace ayther::audio_qa
