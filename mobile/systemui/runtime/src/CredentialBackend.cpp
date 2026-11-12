#include "fahim/security/CredentialBackend.hpp"

namespace fahim::security {

fahim::systemui::VerificationResult ProtectedCredentialVerifier::verify_pin(
    const std::uint8_t* pin, std::size_t length) noexcept {
    if (backend_ == nullptr || !backend_->available() ||
        pin == nullptr || length < fahim::systemui::LockScreenCore::kMinPinLength ||
        length > fahim::systemui::LockScreenCore::kMaxPinLength) {
        return fahim::systemui::VerificationResult::TemporarilyUnavailable;
    }
    return backend_->verify_pin_and_update_policy(pin, length);
}

}  // namespace fahim::security
