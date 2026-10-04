#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ayther::audio_qa {

// Spec 002 (contracts.md C5): the Runtime probes answer with one JSON object per line.
// The object is flattened into paths such as `catalog.poses` or `unreadable_assets[0]`;
// every array and object also keeps an entry with its number of elements, and the root
// object is the empty path.
enum class JsonKind { null_value, boolean, number, string, array, object };

struct JsonField {
    std::string path;
    JsonKind kind{JsonKind::null_value};
    std::string text;
    bool operator==(const JsonField &) const = default;
};

class JsonFields final {
  public:
    JsonFields() = default;
    explicit JsonFields(std::vector<JsonField> fields) : fields_(std::move(fields)) {}

    [[nodiscard]] const JsonField *find(std::string_view path) const noexcept;
    [[nodiscard]] std::optional<std::string> string(std::string_view path) const;
    [[nodiscard]] std::optional<bool> boolean(std::string_view path) const noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
    unsigned_number(std::string_view path) const noexcept;
    [[nodiscard]] std::optional<double> number(std::string_view path) const noexcept;
    // Number of elements of an array or an object.
    [[nodiscard]] std::optional<std::size_t> size(std::string_view path) const noexcept;
    [[nodiscard]] const std::vector<JsonField> &fields() const noexcept { return fields_; }

  private:
    std::vector<JsonField> fields_;
};

inline constexpr std::size_t max_json_depth = 8;
inline constexpr std::size_t max_json_fields = 4096;
inline constexpr std::size_t max_json_bytes = 256U * 1024U;

// nullopt for malformed JSON, a top-level value that is not an object, too much nesting,
// too many fields, duplicate keys, or keys that would make a path ambiguous.
[[nodiscard]] std::optional<JsonFields> parse_json_fields(std::string_view text);

} // namespace ayther::audio_qa
