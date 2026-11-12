#include "fahim/ui/SessionGate.hpp"

namespace fahim::ui {

bool LockScreenSessionGate::can_present(SurfaceRole role) const noexcept {
    const auto state = core_.state();
    const bool locked = state != fahim::systemui::LockState::Unlocked;
    if (!locked) return true;

    switch (role) {
        case SurfaceRole::LockScreen:
        case SurfaceRole::PublicSystemOverlay:
            return true;
        case SurfaceRole::EmergencyDialer:
            return emergency_active_;
        case SurfaceRole::Application:
        default:
            return false;
    }
}

bool LockScreenSessionGate::can_dispatch_application_input() const noexcept {
    return core_.state() == fahim::systemui::LockState::Unlocked;
}

}  // namespace fahim::ui
