#include "status_emitter.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ayther::runtime {

namespace {

constexpr std::string_view status_prefix = "AYTHER_STATUS ";
constexpr char hex_digits[] = "0123456789abcdef";

void append_json_string(std::string& output, const std::string_view value) {
    output.push_back('"');
    for (const unsigned char code_unit : value) {
        switch (code_unit) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (code_unit < 0x20U) {
                output += "\\u00";
                output.push_back(hex_digits[(code_unit >> 4U) & 0x0fU]);
                output.push_back(hex_digits[code_unit & 0x0fU]);
            } else {
                output.push_back(static_cast<char>(code_unit));
            }
            break;
        }
    }
    output.push_back('"');
}

class JsonObject final {
public:
    JsonObject() { json_.push_back('{'); }

    void field(const std::string_view name, const std::string_view value) {
        append_name(name);
        append_json_string(json_, value);
    }

    void field(const std::string_view name, const char* value) {
        field(name, std::string_view{value});
    }

    void field(const std::string_view name, const bool value) {
        append_name(name);
        json_ += value ? "true" : "false";
    }

    void field(const std::string_view name, const std::uint32_t value) {
        append_name(name);
        json_ += std::to_string(value);
    }

    [[nodiscard]] std::string finish() {
        json_.push_back('}');
        return std::move(json_);
    }

private:
    void append_name(const std::string_view name) {
        if (!first_) {
            json_.push_back(',');
        }
        first_ = false;
        append_json_string(json_, name);
        json_.push_back(':');
    }

    std::string json_;
    bool first_{true};
};

[[nodiscard]] std::string serialize_json(const StatusEvent& event) {
    JsonObject json;
    json.field("protocol_version", status_protocol_version);
    std::visit(
        [&json](const auto& status) {
            using Status = std::decay_t<decltype(status)>;
            if constexpr (std::is_same_v<Status, ProbeSucceededStatus>) {
                json.field("event", "probe");
                json.field("ok", true);
                json.field("api", status.api);
                json.field("library_name", status.library_name);
                json.field("library_version", status.library_version);
                json.field("valid_extensions", status.valid_extensions);
                json.field("need_fullpath", status.need_fullpath);
                json.field("block_extract", status.block_extract);
            } else if constexpr (std::is_same_v<Status, ProbeFailedStatus>) {
                json.field("event", "probe");
                json.field("ok", false);
                json.field("reason", error_reason(status.code));
                if (!status.message.empty()) {
                    json.field("message", status.message);
                }
            } else if constexpr (std::is_same_v<Status, ReadyStatus>) {
                json.field("event", "ready");
                json.field("game_id", status.game_id);
                json.field("has_pack", status.has_pack);
                json.field("manifest", status.manifest);
            } else if constexpr (std::is_same_v<Status, NowPlayingStatus>) {
                json.field("event", "now-playing");
                json.field("game_id", status.game_id);
                json.field("title", status.title);
            } else if constexpr (std::is_same_v<Status, WarningStatus>) {
                json.field("event", "warning");
                json.field("reason", error_reason(status.code));
                if (!status.message.empty()) {
                    json.field("message", status.message);
                }
            } else if constexpr (std::is_same_v<Status, CrashTestStatus>) {
                json.field("event", "crash-test");
            } else if constexpr (std::is_same_v<Status, ExitStatus>) {
                json.field("event", "exit");
                if (status.savestate.has_value()) {
                    json.field("savestate", *status.savestate);
                }
            }
        },
        event);
    return json.finish();
}

}  // namespace

StatusEmitter::StatusEmitter(std::FILE& output) noexcept : output_(&output) {}

std::string StatusEmitter::format_line(const StatusEvent& event) {
    const std::string json = serialize_json(event);
    std::string line;
    line.reserve(status_prefix.size() + json.size() + 1U);
    line.append(status_prefix);
    line.append(json);
    line.push_back('\n');
    return line;
}

bool StatusEmitter::emit(const StatusEvent& event) const {
    const std::string line = format_line(event);
    const std::size_t written = std::fwrite(line.data(), 1U, line.size(), output_);
    const int flush_result = std::fflush(output_);
    return written == line.size() && flush_result == 0;
}

}  // namespace ayther::runtime
