#pragma once

#include "LockScreenCore.hpp"

#include <cstdint>

namespace fahim::systemui {

// Bridges the LockScreenCore result to the kernel scheduler's session gate.
// On AArch64 the default transport uses the Fahim EL0 SVC ABI. Host tests may
// inject a deterministic transport; non-AArch64 defaults fail with -ENOSYS.
class KernelSessionControl final {
public:
    using SyscallTransport = std::int64_t (*)(std::uint32_t number,
                                               std::uint64_t argument) noexcept;

    explicit KernelSessionControl(SyscallTransport transport = nullptr) noexcept;

    std::int64_t is_locked(bool& locked) const noexcept;
    std::int64_t set_locked(bool locked) const noexcept;
    std::int64_t synchronize(const LockScreenCore& core) const noexcept;

    static constexpr std::uint32_t kGetSessionLockedSyscall = 8u;
    static constexpr std::uint32_t kSetSessionLockedSyscall = 9u;

private:
    static std::int64_t native_transport(std::uint32_t number,
                                         std::uint64_t argument) noexcept;

    SyscallTransport transport_{nullptr};
};

}  // namespace fahim::systemui
