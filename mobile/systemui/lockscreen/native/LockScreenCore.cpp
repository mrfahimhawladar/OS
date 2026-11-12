#include "LockScreenCore.hpp"

#include <algorithm>
#include <utility>

namespace fahim::systemui {
LockScreenCore::LockScreenCore(CredentialVerifier& verifier, NowFunction now)
    : verifier_(verifier), now_(std::move(now)) {
    if (!now_) now_ = [] { return Clock::now(); };
}
LockScreenCore::~LockScreenCore() { clear_input(); }

void LockScreenCore::secure_zero(void* memory, std::size_t length) noexcept {
    auto* bytes = static_cast<volatile std::uint8_t*>(memory);
    while (length-- > 0) *bytes++ = 0;
}
void LockScreenCore::clear_input() noexcept {
    secure_zero(pin_.data(), pin_.size());
    pin_length_ = 0;
}
void LockScreenCore::lock() noexcept {
    clear_input();
    if (state_ != LockState::Lockout) state_ = LockState::Locked;
}
bool LockScreenCore::append_digit(char digit) noexcept {
    tick();
    if (state_ != LockState::Locked || digit < '0' || digit > '9' ||
        pin_length_ >= pin_.size()) return false;
    pin_[pin_length_++] = static_cast<std::uint8_t>(digit);
    return true;
}
bool LockScreenCore::erase_last_digit() noexcept {
    tick();
    if (state_ != LockState::Locked || pin_length_ == 0) return false;
    pin_[--pin_length_] = 0;
    return true;
}
std::chrono::seconds LockScreenCore::lockout_duration() const noexcept {
    const unsigned exponent = std::min(lockout_level_, 4u);
    return std::chrono::seconds(std::min(30u << exponent, 300u));
}
SubmitResult LockScreenCore::submit_pin() noexcept {
    tick();
    if (state_ != LockState::Locked) {
        return state_ == LockState::Lockout ? SubmitResult::LockedOut
                                            : SubmitResult::UnavailableState;
    }
    if (pin_length_ == 0) return SubmitResult::EmptyInput;
    if (pin_length_ < kMinPinLength) {
        clear_input();
        return SubmitResult::InvalidLength;
    }
    state_ = LockState::Authenticating;
    const VerificationResult verification = verifier_.verify_pin(pin_.data(), pin_length_);
    clear_input();

    switch (verification) {
        case VerificationResult::Accepted:
            failures_ = 0;
            lockout_level_ = 0;
            state_ = LockState::Unlocked;
            return SubmitResult::Accepted;
        case VerificationResult::LockedOut: {
            auto retry = verifier_.retry_after();
            if (retry <= std::chrono::milliseconds::zero()) retry = lockout_duration();
            lockout_until_ = now_() + retry;
            ++lockout_level_;
            state_ = LockState::Lockout;
            return SubmitResult::LockedOut;
        }
        case VerificationResult::TemporarilyUnavailable:
            state_ = LockState::Locked;
            return SubmitResult::BackendUnavailable;
        case VerificationResult::Rejected:
        default:
            ++failures_;
            if (failures_ >= kFailuresBeforeLockout) {
                lockout_until_ = now_() + lockout_duration();
                ++lockout_level_;
                state_ = LockState::Lockout;
                return SubmitResult::LockedOut;
            }
            state_ = LockState::Locked;
            return SubmitResult::Rejected;
    }
}
void LockScreenCore::tick() noexcept {
    if (state_ == LockState::Lockout && now_() >= lockout_until_) {
        clear_input();
        state_ = LockState::Locked;
    }
}
std::chrono::milliseconds LockScreenCore::lockout_remaining() noexcept {
    tick();
    if (state_ != LockState::Lockout) return std::chrono::milliseconds::zero();
    return std::chrono::duration_cast<std::chrono::milliseconds>(lockout_until_ - now_());
}
}  // namespace fahim::systemui
