#include "fahim/security/CredentialBackend.hpp"

namespace fahim::security {

fahim::systemui::VerificationResult ProtectedCredentialVerifier::verify_pin(
    const std::uint8_t* pin, std::size_t length) noexcept {
    last_retry_after_ = std::chrono::seconds(30);
    if (backend_ == nullptr || !backend_->available() ||
        pin == nullptr || length < fahim::systemui::LockScreenCore::kMinPinLength ||
        length > fahim::systemui::LockScreenCore::kMaxPinLength) {
        return fahim::systemui::VerificationResult::TemporarilyUnavailable;
    }
    const auto result = backend_->verify_pin_and_update_policy(pin, length);
    last_retry_after_ = result == fahim::systemui::VerificationResult::LockedOut
        ? backend_->lockout_remaining() : std::chrono::seconds(30);
    if (last_retry_after_ <= std::chrono::milliseconds::zero()) {
        last_retry_after_ = std::chrono::seconds(30);
    }
    return result;
}

}  // namespace fahim::security
