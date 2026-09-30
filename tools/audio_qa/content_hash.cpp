#include "content_hash.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace ayther::audio_qa {
namespace {

constexpr std::array<std::uint32_t, 64> sha256_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

void sha256_block(std::array<std::uint32_t, 8> &state,
                  const std::array<std::uint8_t, 64> &block) noexcept {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        const auto offset = index * 4;
        words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                       (static_cast<std::uint32_t>(block[offset + 1]) << 16U) |
                       (static_cast<std::uint32_t>(block[offset + 2]) << 8U) |
                       static_cast<std::uint32_t>(block[offset + 3]);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        const auto s0 = std::rotr(words[index - 15], 7) ^ std::rotr(words[index - 15], 18) ^
                        (words[index - 15] >> 3U);
        const auto s1 = std::rotr(words[index - 2], 17) ^ std::rotr(words[index - 2], 19) ^
                        (words[index - 2] >> 10U);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }

    auto a = state[0];
    auto b = state[1];
    auto c = state[2];
    auto d = state[3];
    auto e = state[4];
    auto f = state[5];
    auto g = state[6];
    auto h = state[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto upper = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const auto choose = (e & f) ^ (~e & g);
        const auto first = h + upper + choose + sha256_constants[index] + words[index];
        const auto lower = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto second = lower + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

} // namespace

void ContentHasher::update(const std::span<const std::byte> bytes) noexcept {
    byte_size_ += static_cast<std::uint64_t>(bytes.size());
    std::size_t cursor{};
    if (pending_size_ != 0) {
        const auto copied = (std::min)(pending_.size() - pending_size_, bytes.size());
        for (std::size_t index = 0; index < copied; ++index) {
            pending_[pending_size_ + index] = std::to_integer<std::uint8_t>(bytes[index]);
        }
        pending_size_ += copied;
        cursor += copied;
        if (pending_size_ == pending_.size()) {
            sha256_block(state_, pending_);
            pending_size_ = 0;
        }
    }
    while (bytes.size() - cursor >= pending_.size()) {
        std::array<std::uint8_t, 64> block{};
        for (std::size_t index = 0; index < block.size(); ++index) {
            block[index] = std::to_integer<std::uint8_t>(bytes[cursor + index]);
        }
        sha256_block(state_, block);
        cursor += block.size();
    }
    while (cursor < bytes.size()) {
        pending_[pending_size_++] = std::to_integer<std::uint8_t>(bytes[cursor++]);
    }
}

ContentIdentity ContentHasher::finish() const noexcept {
    auto state = state_;
    std::array<std::uint8_t, 128> tail{};
    std::copy_n(pending_.begin(), pending_size_, tail.begin());
    tail[pending_size_] = 0x80U;
    const std::size_t padded = pending_size_ < 56U ? 64U : 128U;
    const auto bit_length = byte_size_ * 8U;
    for (std::size_t index = 0; index < 8; ++index) {
        tail[padded - 1U - index] = static_cast<std::uint8_t>(bit_length >> (index * 8U));
    }
    for (std::size_t offset = 0; offset < padded; offset += 64U) {
        std::array<std::uint8_t, 64> block{};
        std::copy_n(tail.begin() + static_cast<std::ptrdiff_t>(offset), block.size(),
                    block.begin());
        sha256_block(state, block);
    }

    ContentIdentity identity;
    identity.byte_size = byte_size_;
    for (std::size_t index = 0; index < state.size(); ++index) {
        identity.sha256[index * 4] = static_cast<std::uint8_t>(state[index] >> 24U);
        identity.sha256[index * 4 + 1] = static_cast<std::uint8_t>(state[index] >> 16U);
        identity.sha256[index * 4 + 2] = static_cast<std::uint8_t>(state[index] >> 8U);
        identity.sha256[index * 4 + 3] = static_cast<std::uint8_t>(state[index]);
    }
    return identity;
}

ContentIdentity identify_content(const std::span<const std::byte> bytes) noexcept {
    ContentHasher hasher;
    hasher.update(bytes);
    return hasher.finish();
}

} // namespace ayther::audio_qa
