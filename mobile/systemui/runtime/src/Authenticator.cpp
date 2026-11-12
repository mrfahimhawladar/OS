#include "fahim/security/Authenticator.hpp"

#include <algorithm>
#include <cstring>

namespace fahim::security {
namespace {

constexpr std::uint32_t rotate_left(std::uint32_t value, unsigned bits) noexcept {
    return (value << bits) | (value >> (32u - bits));
}

class Sha1 final {
public:
    void update(const std::uint8_t* data, std::size_t length) noexcept {
        if (data == nullptr || length == 0u) return;
        total_bytes_ += length;
        while (length != 0u) {
            const std::size_t count = std::min(length, block_.size() - used_);
            std::memcpy(block_.data() + used_, data, count);
            used_ += count;
            data += count;
            length -= count;
            if (used_ == block_.size()) {
                transform(block_.data());
                used_ = 0u;
            }
        }
    }

    void finish(std::uint8_t digest[20]) noexcept {
        const std::uint64_t bits = static_cast<std::uint64_t>(total_bytes_) * 8u;
        block_[used_++] = 0x80u;
        if (used_ > 56u) {
            while (used_ < block_.size()) block_[used_++] = 0u;
            transform(block_.data());
            used_ = 0u;
        }
        while (used_ < 56u) block_[used_++] = 0u;
        for (int shift = 56; shift >= 0; shift -= 8) {
            block_[used_++] = static_cast<std::uint8_t>(bits >> shift);
        }
        transform(block_.data());
        for (std::size_t i = 0; i < state_.size(); ++i) {
            digest[i * 4u] = static_cast<std::uint8_t>(state_[i] >> 24u);
            digest[i * 4u + 1u] = static_cast<std::uint8_t>(state_[i] >> 16u);
            digest[i * 4u + 2u] = static_cast<std::uint8_t>(state_[i] >> 8u);
            digest[i * 4u + 3u] = static_cast<std::uint8_t>(state_[i]);
        }
        secure_clear(block_.data(), block_.size());
        secure_clear(state_.data(), state_.size() * sizeof(state_[0]));
    }

private:
    void transform(const std::uint8_t block[64]) noexcept {
        std::uint32_t w[80]{};
        for (std::size_t i = 0; i < 16u; ++i) {
            const std::size_t j = i * 4u;
            w[i] = (static_cast<std::uint32_t>(block[j]) << 24u) |
                   (static_cast<std::uint32_t>(block[j + 1u]) << 16u) |
                   (static_cast<std::uint32_t>(block[j + 2u]) << 8u) |
                   static_cast<std::uint32_t>(block[j + 3u]);
        }
        for (std::size_t i = 16u; i < 80u; ++i) {
            w[i] = rotate_left(w[i - 3u] ^ w[i - 8u] ^ w[i - 14u] ^ w[i - 16u], 1u);
        }

        std::uint32_t a = state_[0], b = state_[1], c = state_[2];
        std::uint32_t d = state_[3], e = state_[4];
        for (std::size_t i = 0; i < 80u; ++i) {
            std::uint32_t f = 0u, k = 0u;
            if (i < 20u) {
                f = (b & c) | ((~b) & d); k = 0x5a827999u;
            } else if (i < 40u) {
                f = b ^ c ^ d; k = 0x6ed9eba1u;
            } else if (i < 60u) {
                f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcu;
            } else {
                f = b ^ c ^ d; k = 0xca62c1d6u;
            }
            const std::uint32_t temp = rotate_left(a, 5u) + f + e + k + w[i];
            e = d; d = c; c = rotate_left(b, 30u); b = a; a = temp;
        }
        state_[0] += a; state_[1] += b; state_[2] += c;
        state_[3] += d; state_[4] += e;
        secure_clear(w, sizeof(w));
    }

    std::array<std::uint32_t, 5> state_{
        0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u
    };
    std::array<std::uint8_t, 64> block_{};
    std::size_t used_{0};
    std::size_t total_bytes_{0};
};

void hmac_sha1(const std::uint8_t* key, std::size_t key_length,
               const std::uint8_t* message, std::size_t message_length,
               std::uint8_t output[20]) noexcept {
    std::uint8_t normalized_key[64]{};
    if (key_length > sizeof(normalized_key)) {
        Sha1 hash;
        std::uint8_t digest[20]{};
        hash.update(key, key_length);
        hash.finish(digest);
        std::memcpy(normalized_key, digest, sizeof(digest));
        secure_clear(digest, sizeof(digest));
    } else if (key != nullptr && key_length != 0u) {
        std::memcpy(normalized_key, key, key_length);
    }

    std::uint8_t inner_pad[64]{};
    std::uint8_t outer_pad[64]{};
    for (std::size_t i = 0; i < 64u; ++i) {
        inner_pad[i] = normalized_key[i] ^ 0x36u;
        outer_pad[i] = normalized_key[i] ^ 0x5cu;
    }

    std::uint8_t inner_digest[20]{};
    Sha1 inner;
    inner.update(inner_pad, sizeof(inner_pad));
    inner.update(message, message_length);
    inner.finish(inner_digest);

    Sha1 outer;
    outer.update(outer_pad, sizeof(outer_pad));
    outer.update(inner_digest, sizeof(inner_digest));
    outer.finish(output);

    secure_clear(normalized_key, sizeof(normalized_key));
    secure_clear(inner_pad, sizeof(inner_pad));
    secure_clear(outer_pad, sizeof(outer_pad));
    secure_clear(inner_digest, sizeof(inner_digest));
}

}  // namespace

void secure_clear(void* memory, std::size_t length) noexcept {
    auto* bytes = static_cast<volatile std::uint8_t*>(memory);
    while (length-- != 0u) *bytes++ = 0u;
}

bool compute_totp(const std::uint8_t* secret, std::size_t secret_length,
                  std::uint64_t unix_time_seconds, std::uint32_t period_seconds,
                  std::uint8_t digits, char* output,
                  std::size_t output_capacity) noexcept {
    if (secret == nullptr || secret_length == 0u || secret_length > 64u ||
        period_seconds == 0u || (digits != 6u && digits != 8u) ||
        output == nullptr || output_capacity <= digits) {
        return false;
    }

    const std::uint64_t counter = unix_time_seconds / period_seconds;
    std::uint8_t message[8]{};
    for (std::size_t i = 0; i < message.size(); ++i) {
        message[7u - i] = static_cast<std::uint8_t>(counter >> (i * 8u));
    }

    std::uint8_t digest[20]{};
    hmac_sha1(secret, secret_length, message, sizeof(message), digest);
    const std::size_t offset = digest[19] & 0x0fu;
    const std::uint32_t binary =
        (static_cast<std::uint32_t>(digest[offset] & 0x7fu) << 24u) |
        (static_cast<std::uint32_t>(digest[offset + 1u]) << 16u) |
        (static_cast<std::uint32_t>(digest[offset + 2u]) << 8u) |
        static_cast<std::uint32_t>(digest[offset + 3u]);
    std::uint32_t modulus = 1u;
    for (std::uint8_t i = 0; i < digits; ++i) modulus *= 10u;
    std::uint32_t code = binary % modulus;
    for (std::size_t i = digits; i > 0u; --i) {
        output[i - 1u] = static_cast<char>('0' + (code % 10u));
        code /= 10u;
    }
    output[digits] = '\0';
    secure_clear(message, sizeof(message));
    secure_clear(digest, sizeof(digest));
    return true;
}

}  // namespace fahim::security
