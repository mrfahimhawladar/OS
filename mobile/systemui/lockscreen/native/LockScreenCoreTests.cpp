#include "LockScreenCore.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>

using namespace std::chrono_literals;
using fahim::systemui::CredentialVerifier;
using fahim::systemui::LockScreenCore;
using fahim::systemui::LockState;
using fahim::systemui::SubmitResult;
using fahim::systemui::VerificationResult;

class TestVerifier final : public CredentialVerifier {
public:
    VerificationResult next_result{VerificationResult::Rejected};
    VerificationResult verify_pin(const std::uint8_t* pin,
                                  std::size_t length) noexcept override {
        const bool matches = length == 6 && std::memcmp(pin, "246810", 6) == 0;
        if (next_result == VerificationResult::TemporarilyUnavailable) return next_result;
        return matches ? VerificationResult::Accepted : next_result;
    }
};
static void enter_pin(LockScreenCore& core, const char* pin) {
    for (const char* p = pin; *p; ++p) assert(core.append_digit(*p));
}
int main() {
    auto now = LockScreenCore::TimePoint{};
    TestVerifier verifier;
    LockScreenCore core(verifier, [&now] { return now; });

    assert(core.state() == LockState::Locked);
    assert(core.submit_pin() == SubmitResult::EmptyInput);
    enter_pin(core, "123");
    assert(core.submit_pin() == SubmitResult::InvalidLength);
    assert(core.input_length() == 0);
    assert(!core.append_digit('x'));

    enter_pin(core, "246810");
    assert(core.submit_pin() == SubmitResult::Accepted);
    assert(core.state() == LockState::Unlocked);
    assert(core.input_length() == 0);

    core.lock();
    assert(core.state() == LockState::Locked);
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        enter_pin(core, "111111");
        assert(core.submit_pin() == SubmitResult::Rejected);
    }
    enter_pin(core, "111111");
    assert(core.submit_pin() == SubmitResult::LockedOut);
    assert(core.state() == LockState::Lockout);
    assert(core.lockout_remaining() == 30s);
    assert(!core.append_digit('1'));

    now += 29s;
    core.tick();
    assert(core.state() == LockState::Lockout);
    now += 1s;
    core.tick();
    assert(core.state() == LockState::Locked);

    enter_pin(core, "111111");
    assert(core.submit_pin() == SubmitResult::LockedOut);
    assert(core.lockout_remaining() == 60s);

    now += 60s;
    core.tick();
    core.lock();
    verifier.next_result = VerificationResult::TemporarilyUnavailable;
    enter_pin(core, "1234");
    assert(core.submit_pin() == SubmitResult::BackendUnavailable);
    assert(core.state() == LockState::Locked);
    assert(core.input_length() == 0);

    for (unsigned i = 0; i < LockScreenCore::kMaxPinLength; ++i) {
        assert(core.append_digit('7'));
    }
    assert(!core.append_digit('7'));
    core.clear_input();
    assert(core.input_length() == 0);

    std::cout << "LockScreenCore tests: PASS\n";
    return 0;
}
