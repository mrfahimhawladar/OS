#pragma once

#include "fahim/security/CredentialBackend.hpp"
#include "fahim_credential_protocol.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fahim::security {

enum class TeeCredentialResult : std::uint32_t {
    Ok = FAHIM_CRED_RESULT_OK,
    Rejected = FAHIM_CRED_RESULT_REJECTED,
    LockedOut = FAHIM_CRED_RESULT_LOCKED_OUT,
    NotEnrolled = FAHIM_CRED_RESULT_NOT_ENROLLED,
    AlreadyEnrolled = FAHIM_CRED_RESULT_ALREADY_ENROLLED,
    Invalid = FAHIM_CRED_RESULT_INVALID,
    Unavailable = FAHIM_CRED_RESULT_UNAVAILABLE
};

struct TeeCredentialState {
    bool enrolled{false};
    std::uint32_t failed_attempts{0};
    std::uint64_t retry_after_ms{0};
};

class TeeCredentialTransport {
public:
    virtual ~TeeCredentialTransport() = default;
    virtual bool ready() const noexcept = 0;
    virtual TeeCredentialResult query(TeeCredentialState& state) const noexcept = 0;
    virtual TeeCredentialResult enroll(const std::uint8_t* pin,
                                       std::size_t length,
                                       TeeCredentialState& state) const noexcept = 0;
    virtual TeeCredentialResult verify(const std::uint8_t* pin,
                                       std::size_t length,
                                       TeeCredentialState& state) const noexcept = 0;
};

// GlobalPlatform TEEC client transport. It is deliberately unavailable unless
// the build is explicitly configured with FAHIM_ENABLE_OPTEE_CLIENT.
class OpteeCredentialTransport final : public TeeCredentialTransport {
public:
    OpteeCredentialTransport() noexcept;
    ~OpteeCredentialTransport() override;

    OpteeCredentialTransport(const OpteeCredentialTransport&) = delete;
    OpteeCredentialTransport& operator=(const OpteeCredentialTransport&) = delete;

    bool ready() const noexcept override;
    TeeCredentialResult query(TeeCredentialState& state) const noexcept override;
    TeeCredentialResult enroll(const std::uint8_t* pin, std::size_t length,
                               TeeCredentialState& state) const noexcept override;
    TeeCredentialResult verify(const std::uint8_t* pin, std::size_t length,
                               TeeCredentialState& state) const noexcept override;

private:
    struct Impl;
    Impl* impl_{nullptr};
};

class TeeProtectedCredentialBackend final : public ProtectedCredentialBackend {
public:
    explicit TeeProtectedCredentialBackend(TeeCredentialTransport* transport) noexcept;

    bool available() const noexcept override;
    fahim::systemui::VerificationResult verify_pin_and_update_policy(
        const std::uint8_t* pin, std::size_t length) noexcept override;
    std::chrono::milliseconds lockout_remaining() const noexcept override;

    TeeCredentialResult query_state(TeeCredentialState& state) const noexcept;
    TeeCredentialResult enroll_pin(const std::uint8_t* pin,
                                   std::size_t length) noexcept;

private:
    TeeCredentialTransport* transport_{nullptr};
    std::atomic<std::uint64_t> last_retry_after_ms_{0};
};

}  // namespace fahim::security
