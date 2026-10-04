#include "json_fields.h"

#include <algorithm>
#include <charconv>
#include <system_error>
#include <utility>

namespace ayther::audio_qa {
namespace {

void append_utf8(std::string &output, std::uint32_t code_point) {
    if (code_point < 0x80U) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800U) {
        output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    } else if (code_point < 0x10000U) {
        output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    }
}

class Parser final {
  public:
    explicit Parser(std::string_view text) : text_(text) {}

    [[nodiscard]] std::optional<std::vector<JsonField>> parse() {
        whitespace();
        if (position_ >= text_.size() || text_[position_] != '{' || !value({}, 0))
            return std::nullopt;
        whitespace();
        if (position_ != text_.size())
            return std::nullopt;
        return std::move(fields_);
    }

  private:
    [[nodiscard]] bool value(const std::string &path, std::size_t depth) {
        whitespace();
        if (position_ >= text_.size())
            return false;
        switch (text_[position_]) {
        case '{':
            return object(path, depth + 1U);
        case '[':
            return array(path, depth + 1U);
        case '"': {
            auto text = string();
            return text && add(path, JsonKind::string, std::move(*text));
        }
        default:
            break;
        }
        if (literal("true"))
            return add(path, JsonKind::boolean, "true");
        if (literal("false"))
            return add(path, JsonKind::boolean, "false");
        if (literal("null"))
            return add(path, JsonKind::null_value, {});
        auto text = number();
        return text && add(path, JsonKind::number, std::move(*text));
    }

    [[nodiscard]] bool object(const std::string &path, std::size_t depth) {
        ++position_;
        const auto entry = fields_.size();
        if (depth > max_json_depth || !add(path, JsonKind::object, "0"))
            return false;
        std::size_t count{};
        whitespace();
        if (consume('}'))
            return true;
        for (;;) {
            whitespace();
            auto key = string();
            if (!key || key->empty() || key->find_first_of(".[]") != std::string::npos)
                return false;
            whitespace();
            if (!consume(':'))
                return false;
            const auto child = path.empty() ? *key : path + "." + *key;
            if (std::any_of(fields_.begin(), fields_.end(),
                            [&child](const JsonField &field) { return field.path == child; }))
                return false;
            if (!value(child, depth))
                return false;
            ++count;
            whitespace();
            if (consume('}'))
                break;
            if (!consume(','))
                return false;
        }
        fields_[entry].text = std::to_string(count);
        return true;
    }

    [[nodiscard]] bool array(const std::string &path, std::size_t depth) {
        ++position_;
        const auto entry = fields_.size();
        if (depth > max_json_depth || !add(path, JsonKind::array, "0"))
            return false;
        std::size_t count{};
        whitespace();
        if (consume(']'))
            return true;
        for (;;) {
            std::string child = path;
            child += '[';
            child += std::to_string(count);
            child += ']';
            if (!value(child, depth))
                return false;
            ++count;
            whitespace();
            if (consume(']'))
                break;
            if (!consume(','))
                return false;
        }
        fields_[entry].text = std::to_string(count);
        return true;
    }

    [[nodiscard]] std::optional<std::uint32_t> hex4() {
        if (text_.size() - position_ < 4U)
            return std::nullopt;
        std::uint32_t value{};
        const auto *first = text_.data() + position_;
        const auto parsed = std::from_chars(first, first + 4, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != first + 4)
            return std::nullopt;
        position_ += 4U;
        return value;
    }

    [[nodiscard]] std::optional<std::string> string() {
        if (!consume('"'))
            return std::nullopt;
        std::string result;
        while (position_ < text_.size()) {
            const auto character = static_cast<unsigned char>(text_[position_++]);
            if (character == '"')
                return result;
            if (character < 0x20U)
                return std::nullopt;
            if (character != '\\') {
                result.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= text_.size())
                return std::nullopt;
            switch (text_[position_++]) {
            case '"':
                result.push_back('"');
                break;
            case '\\':
                result.push_back('\\');
                break;
            case '/':
                result.push_back('/');
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u': {
                auto code_point = hex4();
                if (!code_point || (*code_point >= 0xdc00U && *code_point <= 0xdfffU))
                    return std::nullopt;
                if (*code_point >= 0xd800U && *code_point <= 0xdbffU) {
                    if (!literal("\\u"))
                        return std::nullopt;
                    const auto low = hex4();
                    if (!low || *low < 0xdc00U || *low > 0xdfffU)
                        return std::nullopt;
                    *code_point = 0x10000U + ((*code_point - 0xd800U) << 10U) + (*low - 0xdc00U);
                }
                append_utf8(result, *code_point);
                break;
            }
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> number() {
        const auto start = position_;
        (void)consume('-');
        if (!consume('0')) {
            if (!digits())
                return std::nullopt;
        }
        if (consume('.') && !digits())
            return std::nullopt;
        if (consume('e') || consume('E')) {
            if (!consume('+'))
                (void)consume('-');
            if (!digits())
                return std::nullopt;
        }
        return std::string{text_.substr(start, position_ - start)};
    }

    [[nodiscard]] bool digits() {
        const auto start = position_;
        while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9')
            ++position_;
        return position_ != start;
    }

    [[nodiscard]] bool add(const std::string &path, JsonKind kind, std::string text) {
        if (fields_.size() >= max_json_fields)
            return false;
        fields_.push_back({path, kind, std::move(text)});
        return true;
    }

    [[nodiscard]] bool literal(std::string_view expected) {
        if (!text_.substr(position_).starts_with(expected))
            return false;
        position_ += expected.size();
        return true;
    }

    [[nodiscard]] bool consume(char expected) {
        if (position_ >= text_.size() || text_[position_] != expected)
            return false;
        ++position_;
        return true;
    }

    void whitespace() {
        while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\t' ||
                                            text_[position_] == '\r' || text_[position_] == '\n'))
            ++position_;
    }

    std::string_view text_;
    std::size_t position_{};
    std::vector<JsonField> fields_;
};

} // namespace

const JsonField *JsonFields::find(std::string_view path) const noexcept {
    const auto found = std::find_if(fields_.begin(), fields_.end(),
                                    [path](const JsonField &field) { return field.path == path; });
    return found == fields_.end() ? nullptr : &*found;
}

std::optional<std::string> JsonFields::string(std::string_view path) const {
    const auto *field = find(path);
    if (field == nullptr || field->kind != JsonKind::string)
        return std::nullopt;
    return field->text;
}

std::optional<bool> JsonFields::boolean(std::string_view path) const noexcept {
    const auto *field = find(path);
    if (field == nullptr || field->kind != JsonKind::boolean)
        return std::nullopt;
    return field->text == "true";
}

std::optional<std::uint64_t> JsonFields::unsigned_number(std::string_view path) const noexcept {
    const auto *field = find(path);
    if (field == nullptr || field->kind != JsonKind::number)
        return std::nullopt;
    std::uint64_t value{};
    const auto *first = field->text.data();
    const auto *last = first + field->text.size();
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last)
        return std::nullopt;
    return value;
}

std::optional<double> JsonFields::number(std::string_view path) const noexcept {
    const auto *field = find(path);
    if (field == nullptr || field->kind != JsonKind::number)
        return std::nullopt;
    double value{};
    const auto *first = field->text.data();
    const auto *last = first + field->text.size();
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last)
        return std::nullopt;
    return value;
}

std::optional<std::size_t> JsonFields::size(std::string_view path) const noexcept {
    const auto *field = find(path);
    if (field == nullptr || (field->kind != JsonKind::array && field->kind != JsonKind::object))
        return std::nullopt;
    std::size_t value{};
    const auto *first = field->text.data();
    const auto *last = first + field->text.size();
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last)
        return std::nullopt;
    return value;
}

std::optional<JsonFields> parse_json_fields(std::string_view text) {
    if (text.size() > max_json_bytes)
        return std::nullopt;
    auto fields = Parser{text}.parse();
    if (!fields)
        return std::nullopt;
    return JsonFields{std::move(*fields)};
}

} // namespace ayther::audio_qa
