#pragma once

#include "LockScreenCore.hpp"

#include <cstdint>

namespace fahim::ui {

enum class SurfaceRole : std::uint8_t {
    LockScreen,
    PublicSystemOverlay,
    EmergencyDialer,
    Application
};

// All presentation and input routing must consult this gate. It fails closed:
// while not Unlocked, application surfaces are never eligible for composition.
class LockScreenSessionGate final {
public:
    explicit LockScreenSessionGate(const fahim::systemui::LockScreenCore& core) noexcept
        : core_(core) {}

    void set_emergency_active(bool active) noexcept { emergency_active_ = active; }
    bool emergency_active() const noexcept { return emergency_active_; }
    bool can_present(SurfaceRole role) const noexcept;
    bool can_dispatch_application_input() const noexcept;

private:
    const fahim::systemui::LockScreenCore& core_;
    bool emergency_active_{false};
};

}  // namespace fahim::ui
