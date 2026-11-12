#pragma once

#include <cstddef>
#include <cstdint>

namespace fahim::security {

// The vault returns one-time codes and labels only. It never exports TOTP seeds
// or shared secrets into the Normal World/SystemUI process.
struct OneTimeCode {
    char issuer[32]{};
    char label[32]{};
    char code[9]{};
    std::uint32_t seconds_remaining{0};
    std::uint8_t digits{6};
};

class AuthenticatorVault {
public:
    virtual ~AuthenticatorVault() = default;
    virtual bool available() const noexcept = 0;
    virtual std::size_t account_count() const noexcept = 0;
    virtual bool generate_code(std::size_t index,
                               std::uint64_t trusted_unix_time_seconds,
                               OneTimeCode& output) noexcept = 0;
    virtual bool begin_secure_enrollment() noexcept = 0;
};

}  // namespace fahim::security
