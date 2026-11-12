#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fahim::security {

struct AuthenticatorAccount {
    char issuer[32]{};
    char label[32]{};
    std::uint8_t secret[64]{};
    std::size_t secret_length{0};
    std::uint8_t digits{6};
    std::uint32_t period_seconds{30};
};

// Implementations must decrypt/read secrets only inside a trusted service.
// The caller wipes each account buffer after generating a one-time code.
class AuthenticatorVault {
public:
    virtual ~AuthenticatorVault() = default;
    virtual bool available() const noexcept = 0;
    virtual std::size_t account_count() const noexcept = 0;
    virtual bool read_account(std::size_t index,
                              AuthenticatorAccount& account) noexcept = 0;
    virtual bool begin_secure_enrollment() noexcept = 0;
};

struct OneTimeCode {
    char issuer[32]{};
    char label[32]{};
    char code[9]{};
    std::uint32_t seconds_remaining{0};
    std::uint8_t digits{6};
};

bool compute_totp(const std::uint8_t* secret, std::size_t secret_length,
                  std::uint64_t unix_time_seconds, std::uint32_t period_seconds,
                  std::uint8_t digits, char* output,
                  std::size_t output_capacity) noexcept;

void secure_clear(void* memory, std::size_t length) noexcept;

}  // namespace fahim::security
