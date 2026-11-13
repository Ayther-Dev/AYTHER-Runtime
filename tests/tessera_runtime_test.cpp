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
    const auto command =
        '"' + quote(runtime) + " " + arguments + " > " + quote(report.string()) + " 2>&1\"";
    code = std::system(command.c_str());
    std::ifstream input(report, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
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
            const auto scratch =
                std::filesystem::temp_directory_path() /
                ("ayther-native-audio-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                 ".txt");
            int code = -1;
            const auto output =
                run(argv[2],
                    "--provider tessera --rom " + quote(argv[3]) +
                        " --frames 180 --native-input-mask 32 --native-audio-device",
                    scratch, code);
            require(code == 0 && output.find("AYTHER_NATIVE_DONE frames=180") != std::string::npos,
                    "Active audio Runtime process failed");
            std::istringstream lines(output);
            std::string line;
            std::map<std::string, std::string> metrics;
            while (std::getline(lines, line))
                if (line.starts_with("AYTHER_NATIVE_AUDIO "))
                    metrics = fields(line);
            require(!metrics.empty() && std::stoull(metrics.at("source_frames")) > 100000 &&
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
    if (argc != 5 && argc != 6)
        return 2;
    try {
        const std::string runtime = argv[1];
        const std::string rom = argv[2];
        const std::string pack = argv[3];
        const std::string trust = argv[4];
        const bool device_test = argc == 6 && std::string(argv[5]) == "--device";
        const auto scratch =
            std::filesystem::temp_directory_path() /
            ("ayther-native-runtime-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
        int code = -1;
        const auto arguments = "--provider tessera --rom " + quote(rom) + " --pack " + quote(pack) +
                               " --trust-registry " + quote(trust) +
                               " --frames 4 --native-input-mask 1";
        // Tessera v0.2 (megadrive backend) has no observation-mask presentation
        // policy: the pack never confirms, every frame presents the original and
        // the presentation controls of the v0.1 MVP are refused (spec 008).
        const auto output = run(runtime, arguments, scratch, code);
        require(code == 0, "Native Runtime process failed: " + output);
        std::vector<std::map<std::string, std::string>> frames;
        std::map<std::string, std::string> flow, events;
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.starts_with("AYTHER_NATIVE_FRAME "))
                frames.push_back(fields(line));
            if (line.starts_with("AYTHER_NATIVE_FLOW "))
                flow = fields(line);
            if (line.starts_with("AYTHER_NATIVE_EVENTS "))
                events = fields(line);
        }
        require(frames.size() == 4 &&
                    output.find("AYTHER_NATIVE_DONE frames=4") != std::string::npos,
                "Native Runtime did not advance exactly four frames");
        require(output.find("sin política de presentación") != std::string::npos,
                "The megadrive backend was expected to lack the presentation policy");
        require(!flow.empty() && flow.at("frames") == "4" && flow.at("composed_frames") == "0" &&
                    flow.at("fallback_frames") == "4" && flow.at("fallback_policy") == "4" &&
                    flow.at("composed_copy") == "0" && !events.empty() &&
                    events.at("supported") == "0",
                "Native metric counts disagree with the v0.2 original-only presentation");
        for (std::size_t index = 0; index < frames.size(); ++index) {
            require(frames[index].at("frame") == std::to_string(index + 1),
                    "Frame identity is not consecutive");
            require(frames[index].at("presented") == frames[index].at("original"),
                    "A frame presented something other than the original");
            require(!frames[index].at("pcm").empty(), "Original PCM identity is missing");
            require(frames[index].at("transaction") == "0" && frames[index].at("flags") == "0",
                    "A control transaction was confirmed without a policy");
        }
        const auto controls =
            run(runtime,
                "--provider tessera --rom " + quote(rom) + " --pack " + quote(pack) +
                    " --trust-registry " + quote(trust) +
                    " --frames 4 --native-deactivate-at 2 --native-reactivate-at 4",
                scratch, code);
        require(code != 0 &&
                    controls.find("no admite controles de presentación") != std::string::npos,
                "Presentation controls were not refused explicitly: " + controls);
        const auto invalid = run(
            runtime, "--provider tessera --core forbidden.dll --rom " + quote(rom) + " --frames 1",
            scratch, code);
        require(code == 64 && invalid.find("args.provider_conflict") != std::string::npos,
                "Native provider accepted a libretro core");
        const auto invalid_mask =
            run(runtime,
                "--provider tessera --rom " + quote(rom) + " --frames 1 --native-input-mask 256",
                scratch, code);
        require(code != 0 && invalid_mask.find("AYTHER_NATIVE_FRAME ") == std::string::npos,
                "Invalid full-state input advanced the native session");
        const auto invalid_order =
            run(runtime,
                "--provider tessera --rom " + quote(rom) + " --frames 4 --native-deactivate-at 4 " +
                    "--native-reactivate-at 2",
                scratch, code);
        require(code != 0 && invalid_order.find("AYTHER_NATIVE_FRAME ") == std::string::npos,
                "Out-of-order presentation control advanced the native session");
        const auto missing_pack =
            run(runtime,
                "--provider tessera --rom " + quote(rom) + " --pack " + quote(pack + ".missing") +
                    " --trust-registry " + quote(trust) + " --frames 1",
                scratch, code);
        require(code != 0 && missing_pack.find("AYTHER_NATIVE_FRAME ") == std::string::npos,
                "Missing graphics pack advanced the native session");
        if (device_test) {
            const auto probe_output =
                run(runtime, arguments + " --native-visible --native-gpu-probe", scratch, code);
            require(code == 0, "Vulkan readback failed: " + probe_output);
            std::vector<std::map<std::string, std::string>> gpu_frames;
            std::vector<std::map<std::string, std::string>> probe_native_frames;
            std::istringstream probe_lines(probe_output);
            while (std::getline(probe_lines, line)) {
                if (line.starts_with("AYTHER_NATIVE_GPU_FRAME "))
                    gpu_frames.push_back(fields(line));
                if (line.starts_with("AYTHER_NATIVE_FRAME "))
                    probe_native_frames.push_back(fields(line));
            }
            require(probe_native_frames.size() == frames.size(),
                    "GPU path changed the number of native frames");
            for (std::size_t index = 0; index < frames.size(); ++index) {
                require(probe_native_frames[index].at("original") == frames[index].at("original") &&
                            probe_native_frames[index].at("pcm") == frames[index].at("pcm") &&
                            probe_native_frames[index].at("presented") ==
                                frames[index].at("presented"),
                        "GPU path changed original video, audio, or composition");
            }
            require(gpu_frames.size() == 4, "GPU path did not read back four frames");
            for (const auto &gpu_frame : gpu_frames)
                require(gpu_frame.at("magenta_pixels") == "0",
                        "GPU pixels show the pack although v0.2 presents the original");
            const auto device_output =
                run(runtime,
                    "--provider tessera --rom " + quote(rom) + " --pack " + quote(pack) +
                        " --trust-registry " + quote(trust) +
                        " --frames 90 --native-visible --native-audio-device",
                    scratch, code);
            require(code == 0, "Vulkan/audio Runtime process failed: " + device_output);
            std::map<std::string, std::string> presentation, audio, gpu_timestamp;
            std::map<std::string, std::map<std::string, std::string>> costs;
            std::istringstream device_lines(device_output);
            while (std::getline(device_lines, line)) {
                if (line.starts_with("AYTHER_NATIVE_PRESENT "))
                    presentation = fields(line);
                if (line.starts_with("AYTHER_NATIVE_AUDIO "))
                    audio = fields(line);
                if (line.starts_with("AYTHER_NATIVE_GPU_TIMESTAMP "))
                    gpu_timestamp = fields(line);
                if (line.starts_with("AYTHER_NATIVE_COST ")) {
                    auto item = fields(line);
                    costs.emplace(item.at("stage"), std::move(item));
                }
            }
            require(!presentation.empty() && !audio.empty(),
                    "GPU or audio device metrics are absent");
            require(!gpu_timestamp.empty() && gpu_timestamp.at("supported") == "1" &&
                        gpu_timestamp.at("samples") == "90" &&
                        gpu_timestamp.at("scope") == "top_image_ready_bottom" &&
                        std::stoul(gpu_timestamp.at("valid_bits")) > 0 &&
                        std::stod(gpu_timestamp.at("period_ns")) > 0 &&
                        costs.contains("gpu_execute") && costs.contains("gpu_image_wait") &&
                        costs.contains("gpu_command_span") &&
                        costs.at("gpu_execute").at("count") == "60" &&
                        costs.at("gpu_image_wait").at("count") == "60" &&
                        costs.at("gpu_command_span").at("count") == "60" &&
                        std::stod(costs.at("gpu_execute").at("p50")) > 0,
                    "GPU execution timestamps are incomplete");
            require(presentation.at("frames") == "90" && presentation.at("intervals") == "89" &&
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
                        std::stoull(audio.at("callback_calls")) > 0 && audio.contains("episodes") &&
                        audio.contains("missing_samples") && audio.contains("total_ms") &&
                        audio.contains("max_ms"),
                    "Audio device or underrun coverage is incomplete");
            for (const char *stage :
                 {"engine_step", "engine_frame", "runtime_hash", "runtime_device"}) {
                require(costs.contains(stage) && costs.at(stage).at("unit") == "us" &&
                            costs.at(stage).at("warmup") == "30" &&
                            costs.at(stage).at("count") == "60" &&
                            std::stod(costs.at(stage).at("p50")) > 0,
                        "Runtime stage cost is incomplete");
            }
        }
        std::error_code ignored;
        std::filesystem::remove(scratch, ignored);
        std::cout
            << "Native Runtime FX-03 original presentation, PCM and refused controls passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
