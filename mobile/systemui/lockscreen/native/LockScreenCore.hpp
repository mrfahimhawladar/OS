#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace fahim::systemui {
enum class LockState : std::uint8_t { Locked, Authenticating, Lockout, Unlocked };
enum class VerificationResult : std::uint8_t { Accepted, Rejected, TemporarilyUnavailable };
enum class SubmitResult : std::uint8_t {
    UnavailableState, EmptyInput, InvalidLength, Accepted, Rejected, LockedOut, BackendUnavailable
};

// Production implementations must call a trusted OS credential service.
// They must not retain or persist the supplied plaintext buffer.
class CredentialVerifier {
public:
    virtual ~CredentialVerifier() = default;
    virtual VerificationResult verify_pin(const std::uint8_t* pin,
                                           std::size_t length) noexcept = 0;
};

class LockScreenCore final {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using NowFunction = std::function<TimePoint()>;

    static constexpr std::size_t kMinPinLength = 4;
    static constexpr std::size_t kMaxPinLength = 16;
    static constexpr unsigned kFailuresBeforeLockout = 5;

    explicit LockScreenCore(CredentialVerifier& verifier,
                            NowFunction now = [] { return Clock::now(); });
    ~LockScreenCore();

    LockScreenCore(const LockScreenCore&) = delete;
    LockScreenCore& operator=(const LockScreenCore&) = delete;
    LockScreenCore(LockScreenCore&&) = delete;
    LockScreenCore& operator=(LockScreenCore&&) = delete;

    void lock() noexcept;
    bool append_digit(char digit) noexcept;
    bool erase_last_digit() noexcept;
    void clear_input() noexcept;
    SubmitResult submit_pin() noexcept;
    void tick() noexcept;

    LockState state() const noexcept { return state_; }
    std::size_t input_length() const noexcept { return pin_length_; }
    unsigned consecutive_failures() const noexcept { return failures_; }
    std::chrono::milliseconds lockout_remaining() noexcept;

private:
    static void secure_zero(void* memory, std::size_t length) noexcept;
    std::chrono::seconds lockout_duration() const noexcept;

    CredentialVerifier& verifier_;
    NowFunction now_;
    std::array<std::uint8_t, kMaxPinLength> pin_{};
    std::size_t pin_length_{0};
    unsigned failures_{0};
    unsigned lockout_level_{0};
    LockState state_{LockState::Locked};
    TimePoint lockout_until_{};
};
}  // namespace fahim::systemui
