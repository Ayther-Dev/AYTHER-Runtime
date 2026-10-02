#include "pcm_message.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace qa = ayther::audio_qa;
namespace {

void require(const bool condition, const char *const message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::size_t sample_width(const qa::PcmFormat format) {
    switch (format) {
    case qa::PcmFormat::s16le:
        return 2U;
    case qa::PcmFormat::s24le:
        return 3U;
    case qa::PcmFormat::s32le:
    case qa::PcmFormat::f32le:
        return 4U;
    }
    throw std::runtime_error("unsupported_profile_format");
}

std::vector<std::byte> known_pcm(const std::size_t bytes, const std::uint64_t seed) {
    std::vector<std::byte> result(bytes);
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = static_cast<std::byte>((index * 37U + seed * 17U) & 0xffU);
    return result;
}

qa::AudioChunk chunk(const qa::PcmFormat format, const std::uint32_t rate,
                     const std::uint8_t channels, const std::uint64_t begin,
                     const std::uint64_t frames, const std::uint64_t sequence,
                     std::vector<std::byte> bytes) {
    qa::AudioChunk result;
    result.run_id = "run-capture-limits";
    result.capture_point = "session-postmix";
    result.producer_sequence = sequence;
    result.format = {format, rate, channels};
    result.range = {"engine-main-output", rate, begin, begin + frames};
    result.bytes = std::move(bytes);
    result.sha256 = {qa::Availability::known, qa::pcm_sha256(result.bytes), {}};
    return result;
}

qa::AudioChunk round_trip(const qa::AudioChunk &expected, const std::uint64_t channel_sequence) {
    const auto encoded = qa::encode_pcm_message(expected, channel_sequence);
    const auto *message = std::get_if<std::vector<std::byte>>(&encoded);
    require(message != nullptr, "profile_chunk_encoding_failed");
    const auto decoded = qa::decode_pcm_message(*message, channel_sequence);
    const auto *actual = std::get_if<qa::AudioChunk>(&decoded);
    require(actual != nullptr && actual->format.pcm == expected.format.pcm &&
                actual->format.sample_rate == expected.format.sample_rate &&
                actual->format.channels == expected.format.channels &&
                actual->range == expected.range && actual->bytes == expected.bytes &&
                actual->sha256 == expected.sha256,
            "profile_chunk_round_trip_changed_samples");
    return *actual;
}

} // namespace

int main() {
    try {
        constexpr std::uint64_t small_frames = 37U;
        constexpr std::array formats{qa::PcmFormat::s16le, qa::PcmFormat::s24le,
                                     qa::PcmFormat::s32le, qa::PcmFormat::f32le};
        for (std::size_t index = 0; index < formats.size(); ++index) {
            const auto bytes =
                known_pcm(small_frames * 2U * sample_width(formats[index]), index + 1U);
            const auto expected =
                chunk(formats[index], 44'100U, 2U, 0U, small_frames, index + 1U, bytes);
            (void)round_trip(expected, index + 1U);
        }

        constexpr std::uint32_t maximum_rate = 192'000U;
        constexpr std::uint8_t maximum_channels = 8U;
        constexpr std::uint64_t frames_per_chunk = 8'000U;
        constexpr std::size_t frame_bytes = maximum_channels * 4U;
        const auto expected_pcm =
            known_pcm(static_cast<std::size_t>(maximum_rate) * frame_bytes, 91U);
        std::vector<std::byte> reopened_pcm;
        reopened_pcm.reserve(expected_pcm.size());
        std::uint64_t begin{};
        std::uint64_t sequence{100U};
        while (begin < maximum_rate) {
            const auto frames = std::min<std::uint64_t>(frames_per_chunk, maximum_rate - begin);
            const auto byte_begin = static_cast<std::size_t>(begin) * frame_bytes;
            const auto byte_count = static_cast<std::size_t>(frames) * frame_bytes;
            std::vector<std::byte> bytes(
                expected_pcm.begin() + static_cast<std::ptrdiff_t>(byte_begin),
                expected_pcm.begin() + static_cast<std::ptrdiff_t>(byte_begin + byte_count));
            const auto expected = chunk(qa::PcmFormat::s32le, maximum_rate, maximum_channels, begin,
                                        frames, sequence, std::move(bytes));
            const auto reopened = round_trip(expected, sequence);
            reopened_pcm.insert(reopened_pcm.end(), reopened.bytes.begin(), reopened.bytes.end());
            begin += frames;
            ++sequence;
        }
        require(begin == maximum_rate && reopened_pcm == expected_pcm,
                "maximum_profile_volume_changed_samples");

        const auto valid =
            chunk(qa::PcmFormat::s16le, 44'100U, 2U, 0U, 1U, 500U, known_pcm(4U, 7U));
        const auto expect_invalid = [](const qa::AudioChunk &candidate, const char *const message) {
            const auto encoded = qa::encode_pcm_message(candidate, 500U);
            require(std::holds_alternative<qa::PcmMessageError>(encoded) &&
                        std::get<qa::PcmMessageError>(encoded) ==
                            qa::PcmMessageError::invalid_chunk,
                    message);
        };
        auto unsupported = valid;
        unsupported.format.pcm = static_cast<qa::PcmFormat>(4U);
        expect_invalid(unsupported, "unsupported_format_was_converted");
        auto excessive_rate = valid;
        excessive_rate.format.sample_rate = maximum_rate + 1U;
        excessive_rate.range.sample_rate = maximum_rate + 1U;
        expect_invalid(excessive_rate, "excessive_rate_was_converted");
        auto excessive_channels = valid;
        excessive_channels.format.channels = maximum_channels + 1U;
        expect_invalid(excessive_channels, "excessive_channels_were_converted");

        std::printf("pcm_capture_limits_test: formats=4 max_frames=%u "
                    "max_channels=%u bytes=%llu chunks=%llu unsupported=rejected\n",
                    maximum_rate, maximum_channels,
                    static_cast<unsigned long long>(expected_pcm.size()),
                    static_cast<unsigned long long>(sequence - 100U));
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "pcm_capture_limits_test: %s\n", error.what());
        return 1;
    }
}
