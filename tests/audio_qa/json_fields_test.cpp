// Spec 002 (contracts.md C5): the JSON lines of the Runtime probes are read strictly and
// flattened into paths; anything malformed or ambiguous is rejected rather than guessed.
#include "json_fields.h"

#include <iostream>
#include <string>
#include <string_view>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

void probe_line_is_flattened() {
    const auto fields = qa::parse_json_fields(
        R"({"schema":"1.0","opened":true,"signature":"valid","trust":"trusted",)"
        R"("catalog":{"poses":217,"audio_events":14},"unreadable_assets":["a.png","b/c.png"],)"
        R"("game_id":"crc32:665d7df9","extra":null,"ratio":-1.5e3})");
    expect(fields.has_value(), "a probe line is read");
    if (!fields)
        return;
    expect(fields->string("schema") == "1.0" && fields->boolean("opened") == true &&
               fields->string("game_id") == "crc32:665d7df9",
           "strings and booleans keep their values");
    expect(fields->unsigned_number("catalog.poses") == 217U &&
               fields->unsigned_number("catalog.audio_events") == 14U &&
               fields->size("catalog") == 2U,
           "nested objects are flattened with their size");
    expect(fields->size("unreadable_assets") == 2U &&
               fields->string("unreadable_assets[1]") == "b/c.png",
           "arrays are flattened with their size");
    expect(fields->find("extra") != nullptr &&
               fields->find("extra")->kind == qa::JsonKind::null_value,
           "null is kept as such");
    expect(!fields->unsigned_number("ratio") && !fields->unsigned_number("schema") &&
               !fields->string("catalog") && !fields->boolean("missing"),
           "a value is only read as its own kind");
    expect(fields->size("") == 9U, "the root object is the empty path");
}

void escapes_are_decoded() {
    const auto fields = qa::parse_json_fields(
        R"({"path":"C:\\roms\\\"game\".md","line":"a\nb\tc","accent":"\u00e9","emoji":"\ud83d\ude00","slash":"\/"})");
    expect(fields && fields->string("path") == "C:\\roms\\\"game\".md" &&
               fields->string("line") == "a\nb\tc" && fields->string("accent") == "\xc3\xa9" &&
               fields->string("emoji") == "\xf0\x9f\x98\x80" && fields->string("slash") == "/",
           "escapes and surrogate pairs are decoded to UTF-8");
}

void malformed_input_is_rejected() {
    for (const std::string_view text : {
             R"([1,2])",
             R"({"a":1,})",
             R"({a:1})",
             R"({"a":1,"a":2})",
             R"({"a":1} trailing)",
             R"({"a":01})",
             R"({"a":1.})",
             R"({"a":tru})",
             R"({"a":"\x"})",
             R"({"a":"\ud83d"})",
             R"({"a":"\ude00"})",
             R"({"a.b":1})",
             R"({"a[0]":1})",
             R"({"":1})",
             R"({"a":[1,]})",
             R"({"a":{"b":{"c":{"d":{"e":{"f":{"g":{"h":{"i":1}}}}}}}}})",
             "{\"a\":\"line\nbreak\"}",
             "",
         })
        expect(!qa::parse_json_fields(text), std::string{"rejected: "}.append(text));
    expect(qa::parse_json_fields(R"({"a":{"b":{"c":{"d":{"e":{"f":{"g":1}}}}}}})").has_value(),
           "eight levels of nesting are accepted");
    expect(!qa::parse_json_fields(std::string(qa::max_json_bytes + 1U, ' ')),
           "a line above the size limit is rejected");
}

} // namespace

int main() {
    probe_line_is_flattened();
    escapes_are_decoded();
    malformed_input_is_rejected();
    if (failures != 0)
        return 1;
    std::cout << "probe lines are read strictly\n";
    return 0;
}
