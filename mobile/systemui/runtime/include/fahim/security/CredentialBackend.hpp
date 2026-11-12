#pragma once

#include "LockScreenCore.hpp"

#include <cstddef>
#include <cstdint>

namespace fahim::security {

// The implementation must reside in a trusted service/TEE-backed component.
// It must atomically verify the PIN and enforce persistent anti-brute-force
// policy; it must never persist the raw PIN or log it.
class ProtectedCredentialBackend {
public:
    virtual ~ProtectedCredentialBackend() = default;
    virtual bool available() const noexcept = 0;
    virtual fahim::systemui::VerificationResult verify_pin_and_update_policy(
        const std::uint8_t* pin, std::size_t length) noexcept = 0;
};

class ProtectedCredentialVerifier final : public fahim::systemui::CredentialVerifier {
public:
    explicit ProtectedCredentialVerifier(ProtectedCredentialBackend* backend) noexcept
        : backend_(backend) {}

    fahim::systemui::VerificationResult verify_pin(
        const std::uint8_t* pin, std::size_t length) noexcept override;

private:
    ProtectedCredentialBackend* backend_{nullptr};
};

}  // namespace fahim::security
