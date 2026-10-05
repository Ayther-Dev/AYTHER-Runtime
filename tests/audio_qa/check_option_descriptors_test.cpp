// Spec 002, RF-1.5 and RF-1.8: the descriptor table is the single source of the
// `check` options and must correspond to the reference inventory (v0.1.0-beta.8).
#include "check_option_descriptors.h"
#include "model_limits.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qa = ayther::audio_qa;

namespace {

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

std::vector<std::string> string_array(const toml::table &table, std::string_view key) {
    std::vector<std::string> values;
    if (const auto *array = table[key].as_array())
        for (const auto &item : *array)
            if (const auto value = item.value<std::string>())
                values.push_back(*value);
    return values;
}

std::string message(std::initializer_list<std::string_view> parts) {
    std::string result;
    for (const auto part : parts)
        result += part;
    return result;
}

std::vector<std::string> as_strings(std::span<const std::string_view> values) {
    return {values.begin(), values.end()};
}

void test_reference_correspondence(const toml::table &inventory) {
    const auto descriptors = qa::check_option_descriptors();
    std::vector<std::string_view> referenced;
    const auto *options = inventory["option"].as_array();
    expect(options != nullptr, "RF-1.8: the reference inventory lists its options");
    if (options == nullptr)
        return;
    for (const auto &node : *options) {
        const auto *entry = node.as_table();
        if (entry == nullptr || (*entry)["command"].value_or(std::string{}) != "check")
            continue;
        const auto flag = (*entry)["flag"].value_or(std::string{});
        const auto *descriptor = qa::find_check_option(flag);
        expect(descriptor != nullptr,
               message({"RF-1.8: reference option ", flag, " has a descriptor"}));
        if (descriptor == nullptr)
            continue;
        referenced.push_back(descriptor->flag);
        if (descriptor->correspondence != qa::ReferenceCorrespondence::same) {
            expect(!descriptor->requirements.empty(),
                   message({"RF-1.8: changed option ", flag, " cites its requirements"}));
            continue;
        }
        expect((*entry)["required"].value_or(false) == descriptor->required,
               message({"RF-1.5: ", flag, " keeps its obligation"}));
        expect((*entry)["repeatable"].value_or(false) == descriptor->repeatable,
               message({"RF-1.5: ", flag, " keeps its repeatability"}));
        expect((*entry)["default"].value_or(std::string{}) ==
                   std::string{descriptor->default_value},
               message({"RF-1.5: ", flag, " keeps its default"}));
        expect(string_array(*entry, "values") == as_strings(descriptor->values),
               message({"RF-1.5: ", flag, " keeps its accepted values"}));
    }
    for (const auto &descriptor : descriptors) {
        if (std::find(referenced.begin(), referenced.end(), descriptor.flag) != referenced.end())
            continue;
        expect(descriptor.correspondence == qa::ReferenceCorrespondence::added &&
                   !descriptor.requirements.empty(),
               message({"RF-1.8: option ", descriptor.flag,
                        " absent from the reference is declared as added with its requirements"}));
    }
}

void test_fixed_subcommands(const toml::table &inventory) {
    const auto fixed = qa::fixed_option_descriptors();
    std::size_t matched = 0;
    for (const auto &node : *inventory["option"].as_array()) {
        const auto *entry = node.as_table();
        const auto command = (*entry)["command"].value_or(std::string{});
        if (command == "check")
            continue;
        const auto flag = (*entry)["flag"].value_or(std::string{});
        const auto found = std::find_if(fixed.begin(), fixed.end(), [&](const auto &item) {
            return item.command == command && item.flag == flag;
        });
        expect(found != fixed.end(), message({"RF-1.8: ", command, " ", flag, " is unchanged"}));
        if (found == fixed.end())
            continue;
        ++matched;
        expect((*entry)["required"].value_or(false) == found->required,
               message({"RF-1.8: ", command, " ", flag, " keeps its obligation"}));
        expect((*entry)["default"].value_or(std::string{}) == std::string{found->default_value},
               message({"RF-1.8: ", command, " ", flag, " keeps its default"}));
        expect(string_array(*entry, "values") == as_strings(found->values),
               message({"RF-1.8: ", command, " ", flag, " keeps its accepted values"}));
    }
    expect(matched == fixed.size(), "RF-1.8: query and audit expose the same options");
}

void test_limits(const toml::table &inventory) {
    const auto limits = inventory["limits"];
    expect(limits["check_arguments"].value_or(0U) == qa::max_check_arguments,
           "RF-1.5: argument limit unchanged");
    expect(limits["check_option_value_bytes"].value_or(0U) == qa::max_check_option_value_bytes,
           "RF-1.5: value limit unchanged");
    expect(limits["check_takes"].value_or(0U) == qa::max_check_takes,
           "RF-1.5: take limit unchanged");
    expect(limits["identity_bytes"].value_or(0U) == qa::max_identity_bytes,
           "RF-1.5: identity limit unchanged");
}

void test_defaults_match_model() {
    const qa::CheckOptions defaults;
    for (const auto &descriptor : qa::check_option_descriptors()) {
        if (descriptor.repeatable)
            continue;
        expect(defaults.*(descriptor.text_field) == descriptor.default_value,
               message({"RF-1.5: the CheckOptions default of ", descriptor.flag,
                        " is the descriptor default"}));
    }
}

void check_inventory_against_table(const toml::table &parsed, std::string_view label) {
    const std::string prefix = message({"RF-1.8: ", label, ": "});
    expect(parsed["schema"].value_or(0) == 1, message({prefix, "keeps schema 1"}));
    test_limits(parsed);
    std::size_t check_options = 0;
    const auto *options = parsed["option"].as_array();
    expect(options != nullptr, message({prefix, "lists its options"}));
    if (options == nullptr)
        return;
    for (const auto &node : *options) {
        const auto *entry = node.as_table();
        if ((*entry)["command"].value_or(std::string{}) != "check")
            continue;
        ++check_options;
        const auto flag = (*entry)["flag"].value_or(std::string{});
        const auto *descriptor = qa::find_check_option(flag);
        expect(descriptor != nullptr, message({prefix, "option ", flag, " is in the table"}));
        if (descriptor == nullptr)
            continue;
        expect((*entry)["required"].value_or(false) == descriptor->required &&
                   (*entry)["repeatable"].value_or(false) == descriptor->repeatable &&
                   (*entry)["default"].value_or(std::string{}) ==
                       std::string{descriptor->default_value} &&
                   string_array(*entry, "values") == as_strings(descriptor->values),
               message({prefix, "option ", flag, " matches the table"}));
    }
    expect(check_options == qa::check_option_descriptors().size(),
           message({prefix, "lists every check option"}));
}

void test_published_inventory_matches_table() {
    const auto parsed = toml::parse(qa::format_check_option_inventory("test-ref", "test-commit"));
    expect(parsed["ref"].value_or(std::string{}) == "test-ref" &&
               parsed["commit"].value_or(std::string{}) == "test-commit",
           "RF-1.8: the published inventory identifies its version");
    check_inventory_against_table(parsed, "published inventory");
}

// Runs a command line and returns its standard output and exit status.
std::pair<std::string, int> run_command(const std::string &command_line) {
#ifdef _WIN32
    // cmd.exe strips the outer quotes of the whole line.
    const std::string command = "\"" + command_line + "\"";
    FILE *pipe = _popen(command.c_str(), "r");
#else
    FILE *pipe = popen(command_line.c_str(), "r");
#endif
    if (pipe == nullptr)
        return {{}, -1};
    std::string output;
    std::array<char, 4096> buffer{};
    while (const auto read = std::fread(buffer.data(), 1, buffer.size(), pipe))
        output.append(buffer.data(), read);
#ifdef _WIN32
    const int status = _pclose(pipe);
#else
    const int status = pclose(pipe);
#endif
    return {output, status};
}

std::string quoted(const std::string &path) {
#ifdef _WIN32
    return "\"" + path + "\"";
#else
    return "'" + path + "'";
#endif
}

bool full_commit(std::string_view commit) {
    return commit.size() == 40U && std::all_of(commit.begin(), commit.end(), [](char digit) {
               return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
           });
}

// Runs `<cli> options --format toml` and compares its output with the table. D-3 (campaign
// 2026-10-04, BR-182; C5): like the reference inventory, it names the full commit of the
// sources it was built from, the HEAD of `source` when that is a git checkout.
void test_cli_inventory_matches_table(const std::string &cli, const std::string &source) {
    const auto [output, status] = run_command(quoted(cli) + " options --format toml");
    expect(status == 0, "RF-1.8: `options --format toml` succeeds");
    if (status != 0)
        return;
    const auto parsed = toml::parse(output);
    check_inventory_against_table(parsed, "CLI inventory");
    const auto commit = parsed["commit"].value_or(std::string{});
    expect(full_commit(commit),
           message({"C5, RF-1.8: the CLI inventory names the full commit of its build, got \"",
                    commit, "\""}));
    if (source.empty())
        return;
    auto [head, git_status] = run_command("git -C " + quoted(source) + " rev-parse HEAD");
    while (!head.empty() && (head.back() == '\n' || head.back() == '\r'))
        head.pop_back();
    if (git_status == 0 && full_commit(head))
        expect(commit == head, message({"C5: the inventory commit ", commit,
                                        " is the HEAD of the sources ", head}));
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2 || argc > 4) {
        std::cerr << "usage: check_option_descriptors_test <reference-inventory.toml> [cli "
                     "[source-directory]]\n";
        return 2;
    }
    try {
        const auto inventory = toml::parse_file(argv[1]);
        test_reference_correspondence(inventory);
        test_fixed_subcommands(inventory);
        test_limits(inventory);
        test_defaults_match_model();
        test_published_inventory_matches_table();
        if (argc >= 3)
            test_cli_inventory_matches_table(argv[2], argc == 4 ? argv[3] : std::string{});
    } catch (const toml::parse_error &error) {
        std::cerr << "FAIL: reference inventory unreadable: " << error.description() << '\n';
        return 1;
    }
    if (failures != 0)
        return 1;
    std::cout << "check option descriptors correspond to the reference inventory\n";
    return 0;
}
