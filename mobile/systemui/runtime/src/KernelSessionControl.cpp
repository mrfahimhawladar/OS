#include "KernelSessionControl.hpp"

namespace fahim::systemui {

KernelSessionControl::KernelSessionControl(SyscallTransport transport) noexcept
    : transport_(transport != nullptr ? transport : &KernelSessionControl::native_transport) {}

std::int64_t KernelSessionControl::native_transport(
    std::uint32_t number, std::uint64_t argument) noexcept {
#if defined(__aarch64__)
    register std::uint64_t x0 __asm__("x0") = argument;
    register std::uint64_t x8 __asm__("x8") = number;
    __asm__ volatile("svc #0"
                     : "+r"(x0)
                     : "r"(x8)
                     : "memory", "cc");
    return static_cast<std::int64_t>(x0);
#else
    (void)number;
    (void)argument;
    return -38;  // ENOSYS: no native Fahim OS SVC transport on host platforms.
#endif
}

std::int64_t KernelSessionControl::is_locked(bool& locked) const noexcept {
    const std::int64_t result = transport_(kGetSessionLockedSyscall, 0u);
    if (result < 0) return result;
    if (result > 1) return -5;  // EIO: kernel returned an invalid lock state.
    locked = result != 0;
    return 0;
}

std::int64_t KernelSessionControl::set_locked(bool locked) const noexcept {
    return transport_(kSetSessionLockedSyscall, locked ? 1u : 0u);
}

std::int64_t KernelSessionControl::synchronize(
    const LockScreenCore& core) const noexcept {
    // Every state other than verifier-approved Unlocked remains kernel-locked.
    const bool locked = core.state() != LockState::Unlocked;
    return set_locked(locked);
}

}  // namespace fahim::systemui
