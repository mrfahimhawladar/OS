#pragma once

#include "fahim/security/CredentialBackend.hpp"
#include "fahim_credential_protocol.h"
#include "fahim_tui_protocol.h"

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


enum class SecureEnrollmentResult : std::uint32_t {
    Enrolled = FAHIM_PROVISION_RESULT_OK,
    NotEnrolled = FAHIM_PROVISION_RESULT_NOT_ENROLLED,
    AlreadyEnrolled = FAHIM_PROVISION_RESULT_ALREADY_ENROLLED,
    PinMismatch = FAHIM_PROVISION_RESULT_PIN_MISMATCH,
    Cancelled = FAHIM_PROVISION_RESULT_CANCELLED,
    Unavailable = FAHIM_PROVISION_RESULT_UNAVAILABLE,
    Invalid = FAHIM_PROVISION_RESULT_INVALID
};

// Starts the Secure World provisioning flow. This method accepts no PIN
// parameter: the signed provisioner TA obtains both entries through the
// OP-TEE core Trusted UI PTA and secure board input/display driver.
class OpteeCredentialProvisioner final {
public:
    OpteeCredentialProvisioner() noexcept;
    ~OpteeCredentialProvisioner();

    OpteeCredentialProvisioner(const OpteeCredentialProvisioner&) = delete;
    OpteeCredentialProvisioner& operator=(const OpteeCredentialProvisioner&) = delete;

    SecureEnrollmentResult enroll_initial_pin() noexcept;

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

/*
 * Owns the TEEC transport, protected backend and LockScreen verifier in the
 * correct lifetime order. Construct this in the trusted Lock Screen service
 * before constructing LockScreenCore with service.verifier().
 */
class TeeCredentialService final {
public:
    TeeCredentialService() noexcept;
    TeeCredentialService(const TeeCredentialService&) = delete;
    TeeCredentialService& operator=(const TeeCredentialService&) = delete;

    bool available() const noexcept { return backend_.available(); }
    ProtectedCredentialVerifier& verifier() noexcept { return verifier_; }
    TeeProtectedCredentialBackend& backend() noexcept { return backend_; }
    OpteeCredentialProvisioner& provisioner() noexcept { return provisioner_; }
    SecureEnrollmentResult enroll_initial_pin() noexcept { return provisioner_.enroll_initial_pin(); }
    fahim::systemui::LockScreenCore& lock_screen_core() noexcept { return core_; }

private:
    OpteeCredentialTransport transport_{};
    TeeProtectedCredentialBackend backend_;
    ProtectedCredentialVerifier verifier_;
    fahim::systemui::LockScreenCore core_;
    OpteeCredentialProvisioner provisioner_{};
};

}  // namespace fahim::security
