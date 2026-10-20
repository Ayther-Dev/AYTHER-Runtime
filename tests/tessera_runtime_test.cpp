#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string quote(const std::string &path) { return '"' + path + '"'; }

std::string run(const std::string &runtime, const std::string &arguments,
                const std::filesystem::path &report, int &code) {
    const auto command = '"' + quote(runtime) + " " + arguments + " > " +
                         quote(report.string()) + " 2>&1\"";
    code = std::system(command.c_str());
    std::ifstream input(report, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

std::map<std::string, std::string> fields(const std::string &line) {
    std::istringstream input(line);
    std::map<std::string, std::string> result;
    std::string word;
    input >> word;
    while (input >> word) {
        const auto separator = word.find('=');
        if (separator != std::string::npos)
            result[word.substr(0, separator)] = word.substr(separator + 1);
    }
    return result;
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 4 && std::string(argv[1]) == "--active-audio") {
        try {
            const auto scratch = std::filesystem::temp_directory_path() /
                ("ayther-native-audio-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
            int code = -1;
            const auto output = run(argv[2], "--provider tessera --rom " +
                quote(argv[3]) +
                " --frames 180 --native-input-mask 32 --native-audio-device",
                scratch, code);
            require(code == 0 && output.find("AYTHER_NATIVE_DONE frames=180") !=
                std::string::npos, "Active audio Runtime process failed");
            std::istringstream lines(output);
            std::string line;
            std::map<std::string, std::string> metrics;
            while (std::getline(lines, line))
                if (line.starts_with("AYTHER_NATIVE_AUDIO ")) metrics = fields(line);
            require(!metrics.empty() &&
                    std::stoull(metrics.at("source_frames")) > 100000 &&
                    std::stoull(metrics.at("active_samples")) > 1000 &&
                    std::stoi(metrics.at("source_peak")) > 16,
                    "FX-02 active PCM was not delivered to the audio device");
            std::error_code ignored;
            std::filesystem::remove(scratch, ignored);
            std::cout << "Native Runtime FX-02 active device audio passed\n";
            return 0;
        } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc != 5 && argc != 6) return 2;
    try {
        const std::string runtime = argv[1];
        const std::string rom = argv[2];
        const std::string pack = argv[3];
        const std::string trust = argv[4];
        const bool device_test = argc == 6 && std::string(argv[5]) == "--device";
        const auto scratch = std::filesystem::temp_directory_path() /
            ("ayther-native-runtime-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
        int code = -1;
        const auto arguments = "--provider tessera --rom " + quote(rom) +
            " --pack " + quote(pack) + " --trust-registry " + quote(trust) +
            " --frames 4 --native-deactivate-at 2 --native-reactivate-at 4";
        const auto output = run(runtime, arguments, scratch, code);
        require(code == 0, "Native Runtime process failed: " + output);
        std::vector<std::map<std::string, std::string>> frames;
        std::map<std::string, std::string> flow, events;
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.starts_with("AYTHER_NATIVE_FRAME ")) frames.push_back(fields(line));
            if (line.starts_with("AYTHER_NATIVE_FLOW ")) flow = fields(line);
            if (line.starts_with("AYTHER_NATIVE_EVENTS ")) events = fields(line);
        }
        require(frames.size() == 4 && output.find("AYTHER_NATIVE_DONE frames=4") !=
                                           std::string::npos,
                "Native Runtime did not advance exactly four frames");
        require(!flow.empty() && flow.at("frames") == "4" &&
                    flow.at("composed_frames") == "2" &&
                    flow.at("fallback_frames") == "2" &&
                    flow.at("fallback_policy") == "2" &&
                    std::stoull(flow.at("composed_copy")) == 2 * 320 * 224 * 4 &&
                    !events.empty() && events.at("losses") == "0" &&
                    events.at("emitted") == events.at("retained"),
                "Native metric counts disagree with FX-03 presentation controls");
        for (std::size_t index = 0; index < frames.size(); ++index) {
            require(frames[index].at("frame") == std::to_string(index + 1),
                    "Frame identity is not consecutive");
            require(frames[index].at("original") == frames[0].at("original"),
                    "Original video changed across presentation controls");
            require(!frames[index].at("pcm").empty(), "Original PCM identity is missing");
        }
        require(frames[0].at("presented") == frames[3].at("presented") &&
                    frames[0].at("presented") != frames[1].at("presented") &&
                    frames[1].at("presented") == frames[1].at("original") &&
                    frames[2].at("presented") == frames[2].at("original"),
                "Activation, deactivation, or original fallback is incorrect");
        require(frames[0].at("transaction") == "1" &&
                    frames[1].at("transaction") == "2" &&
                    frames[3].at("transaction") == "3" &&
                    frames[0].at("flags") == "1" && frames[1].at("flags") == "1" &&
                    frames[3].at("flags") == "1",
                "Applied control confirmation did not propagate");
        const auto invalid = run(runtime, "--provider tessera --core forbidden.dll --rom " +
                                quote(rom) + " --frames 1", scratch, code);
        require(code != 0 && invalid.find("incompatibles") != std::string::npos,
                "Native provider accepted a libretro core");
        const auto invalid_mask = run(runtime, "--provider tessera --rom " +
            quote(rom) + " --frames 1 --native-input-mask 256", scratch, code);
        require(code != 0 && invalid_mask.find("AYTHER_NATIVE_FRAME ") ==
                    std::string::npos,
                "Invalid full-state input advanced the native session");
        const auto invalid_order = run(runtime, "--provider tessera --rom " +
            quote(rom) + " --frames 4 --native-deactivate-at 4 " +
            "--native-reactivate-at 2", scratch, code);
        require(code != 0 && invalid_order.find("AYTHER_NATIVE_FRAME ") ==
                    std::string::npos,
                "Out-of-order presentation control advanced the native session");
        const auto missing_pack = run(runtime, "--provider tessera --rom " +
            quote(rom) + " --pack " + quote(pack + ".missing") +
            " --trust-registry " + quote(trust) + " --frames 1", scratch, code);
        require(code != 0 && missing_pack.find("AYTHER_NATIVE_FRAME ") ==
                    std::string::npos,
                "Missing graphics pack advanced the native session");
        if (device_test) {
            const auto probe_output = run(runtime,
                arguments + " --native-visible --native-gpu-probe", scratch, code);
            require(code == 0, "Vulkan readback failed: " + probe_output);
            std::vector<std::map<std::string, std::string>> gpu_frames;
            std::istringstream probe_lines(probe_output);
            while (std::getline(probe_lines, line))
                if (line.starts_with("AYTHER_NATIVE_GPU_FRAME "))
                    gpu_frames.push_back(fields(line));
            require(gpu_frames.size() == 4 &&
                    std::stoull(gpu_frames[0].at("magenta_pixels")) > 0 &&
                    gpu_frames[0].at("hash") == gpu_frames[3].at("hash") &&
                    gpu_frames[0].at("hash") != gpu_frames[1].at("hash") &&
                    gpu_frames[1].at("hash") == gpu_frames[2].at("hash") &&
                    gpu_frames[1].at("magenta_pixels") == "0",
                    "GPU pixels did not follow the FX-03 activation and fallback policy");
            const auto device_output = run(runtime,
                "--provider tessera --rom " + quote(rom) + " --pack " + quote(pack) +
                " --trust-registry " + quote(trust) +
                " --frames 90 --native-visible --native-audio-device", scratch, code);
            require(code == 0, "Vulkan/audio Runtime process failed: " + device_output);
            std::map<std::string, std::string> presentation, audio;
            std::map<std::string, std::map<std::string, std::string>> costs;
            std::istringstream device_lines(device_output);
            while (std::getline(device_lines, line)) {
                if (line.starts_with("AYTHER_NATIVE_PRESENT ")) presentation = fields(line);
                if (line.starts_with("AYTHER_NATIVE_AUDIO ")) audio = fields(line);
                if (line.starts_with("AYTHER_NATIVE_COST ")) {
                    auto item = fields(line);
                    costs.emplace(item.at("stage"), std::move(item));
                }
            }
            require(!presentation.empty() && !audio.empty(),
                    "GPU or audio device metrics are absent");
            require(presentation.at("frames") == "90" &&
                    presentation.at("intervals") == "89" &&
                    presentation.at("validation_errors") == "0" &&
                    presentation.at("effective_timestamps") == "unavailable",
                    "GPU presentation delivery coverage is incomplete");
            const auto p50 = std::stod(presentation.at("p50_ms"));
            const auto p95 = std::stod(presentation.at("p95_ms"));
            const auto p99 = std::stod(presentation.at("p99_ms"));
            const auto maximum = std::stod(presentation.at("max_ms"));
            require(p50 > 0 && p50 <= p95 && p95 <= p99 && p99 <= maximum,
                    "Presentation intervals are not valid monotonic percentiles");
            require(std::stoull(audio.at("device")) > 0 &&
                    std::stoull(audio.at("source_frames")) > 0 &&
                    audio.contains("episodes") && audio.contains("missing_samples") &&
                    audio.contains("total_ms") && audio.contains("max_ms"),
                    "Audio device or underrun coverage is incomplete");
            for (const char *stage : {"engine_step", "runtime_hash", "runtime_device"}) {
                require(costs.contains(stage) && costs.at(stage).at("unit") == "us" &&
                        costs.at(stage).at("warmup") == "30" &&
                        costs.at(stage).at("count") == "60" &&
                        std::stod(costs.at(stage).at("p50")) > 0,
                        "Runtime stage cost is incomplete");
            }
        }
        std::error_code ignored;
        std::filesystem::remove(scratch, ignored);
        std::cout << "Native Runtime FX-03 activation, fallback, PCM and control results passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
