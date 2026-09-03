#include "status_emitter.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

void check(const bool condition, const char* description) {
    if (condition) {
        ++passed;
    } else {
        ++failed;
    }
    std::fprintf(stderr, "  [%s] %s\n", condition ? " OK " : "FAIL", description);
}

std::string hostile_text() {
    std::string text = "quote \" slash\\ control";
    text.push_back('\x01');
    text += " newline\nUTF-8: español 日本";
    return text;
}

std::vector<ayther::runtime::StatusEvent> fixture_events() {
    using namespace ayther::runtime;
    return {
        ProbeSucceededStatus{1, hostile_text(), "v1", "md|bin", false, true},
        ProbeFailedStatus{hostile_text()},
        ReadyStatus{hostile_text(), true, std::string{"manifest"} + '\x02'},
        NowPlayingStatus{hostile_text(), hostile_text()},
        WarningStatus{hostile_text()},
        CrashTestStatus{},
        ExitStatus{std::nullopt},
        ExitStatus{std::string{"C:\\saves\\quoted\"\n日本.bin"}},
    };
}

int emit_fixture() {
    ayther::runtime::StatusEmitter emitter{*stdout};
    for (const auto& event : fixture_events()) {
        if (!emitter.emit(event)) {
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main(const int argc, char* argv[]) {
    using namespace ayther::runtime;

    if (argc == 2 && std::string_view{argv[1]} == "--json-fixture") {
        return emit_fixture();
    }

    std::fprintf(stderr, "== status_emitter_test (MAD-007) ==\n");

    const std::string escaped = StatusEmitter::format_line(
        ReadyStatus{hostile_text(), true, std::string{"manifest"} + '\x02'});
    check(escaped.starts_with("AYTHER_STATUS {\"event\":\"ready\","),
          "record has the protocol prefix and typed event name");
    check(escaped.ends_with("}\n"), "record ends in exactly one line terminator");
    check(std::count(escaped.begin(), escaped.end(), '\n') == 1,
          "embedded newlines cannot split a protocol record");
    check(escaped.find("quote \\\" slash\\\\") != std::string::npos,
          "quotes and reverse slashes are JSON escaped");
    check(escaped.find("control\\u0001") != std::string::npos &&
              escaped.find("manifest\\u0002") != std::string::npos,
          "control bytes use JSON unicode escapes");
    check(escaped.find("newline\\n") != std::string::npos,
          "newlines use a JSON escape sequence");
    check(escaped.find("español 日本") != std::string::npos,
          "valid UTF-8 is preserved");
    check(escaped.find("\"has_pack\":true") != std::string::npos,
          "boolean fields are emitted as JSON booleans");

    check(StatusEmitter::format_line(
              ProbeSucceededStatus{1, "core", "v1", "md", true, false}) ==
              "AYTHER_STATUS {\"event\":\"probe\",\"ok\":true,\"api\":1,"
              "\"library_name\":\"core\",\"library_version\":\"v1\","
              "\"valid_extensions\":\"md\",\"need_fullpath\":true,"
              "\"block_extract\":false}\n",
          "successful probe contains typed metadata");
    check(StatusEmitter::format_line(ProbeFailedStatus{"no_carga"}) ==
              "AYTHER_STATUS {\"event\":\"probe\",\"ok\":false,"
              "\"reason\":\"no_carga\"}\n",
          "failed probe contains its reason");
    check(StatusEmitter::format_line(NowPlayingStatus{"id", "title"}) ==
              "AYTHER_STATUS {\"event\":\"now-playing\",\"game_id\":\"id\","
              "\"title\":\"title\"}\n",
          "now-playing contains identity and title");
    check(StatusEmitter::format_line(WarningStatus{"warning"}) ==
              "AYTHER_STATUS {\"event\":\"warning\",\"reason\":\"warning\"}\n",
          "warning contains its reason");
    check(StatusEmitter::format_line(CrashTestStatus{}) ==
              "AYTHER_STATUS {\"event\":\"crash-test\"}\n",
          "crash-test has no invented fields");
    check(StatusEmitter::format_line(ExitStatus{std::nullopt}) ==
              "AYTHER_STATUS {\"event\":\"exit\"}\n",
          "exit omits an absent savestate");
    check(StatusEmitter::format_line(ExitStatus{"C:\\save\".bin"}) ==
              "AYTHER_STATUS {\"event\":\"exit\","
              "\"savestate\":\"C:\\\\save\\\".bin\"}\n",
          "exit preserves and escapes the savestate path");

    std::unique_ptr<std::FILE, decltype(&std::fclose)> output{std::tmpfile(),
                                                              &std::fclose};
    check(output != nullptr, "temporary output stream is available");
    if (output != nullptr) {
        const StatusEvent event = WarningStatus{hostile_text()};
        const std::string expected = StatusEmitter::format_line(event);
        StatusEmitter emitter{*output};
        check(emitter.emit(event), "emitter writes and flushes one complete record");
        std::rewind(output.get());
        std::vector<char> bytes(expected.size());
        const std::size_t read =
            std::fread(bytes.data(), 1, bytes.size(), output.get());
        check(read == expected.size() &&
                  std::string{bytes.begin(), bytes.end()} == expected,
              "emitted bytes exactly match the formatted record");
    }

    std::fprintf(stderr, "\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
