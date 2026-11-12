#include "fahim/ui/InputRouter.hpp"

#include <cstdint>

namespace fahim::ui {

LockScreenInputRouter::LockScreenInputRouter(
    std::uint32_t swipe_threshold_px) noexcept
    : swipe_threshold_px_(swipe_threshold_px == 0u ? 72u : swipe_threshold_px) {}

void LockScreenInputRouter::set_locked(bool locked) noexcept {
    locked_ = locked;
    pointer_active_ = false;
}

bool LockScreenInputRouter::contains(Rect rect, std::int32_t x,
                                     std::int32_t y) noexcept {
    if (rect.width == 0u || rect.height == 0u) return false;
    const std::int64_t right = static_cast<std::int64_t>(rect.x) + rect.width;
    const std::int64_t bottom = static_cast<std::int64_t>(rect.y) + rect.height;
    return x >= rect.x && y >= rect.y && x < right && y < bottom;
}

InputDecision LockScreenInputRouter::route(const PointerEvent& event) noexcept {
    if (!locked_) return {InputTarget::Application, LockScreenAction::None};

    switch (event.phase) {
        case PointerPhase::Down:
            pointer_active_ = true;
            start_x_ = event.x;
            start_y_ = event.y;
            return {InputTarget::LockScreen, LockScreenAction::None};

        case PointerPhase::Move:
            return {InputTarget::LockScreen, LockScreenAction::None};

        case PointerPhase::Up: {
            if (!pointer_active_) return {InputTarget::LockScreen, LockScreenAction::None};
            pointer_active_ = false;
            const std::int64_t dy = static_cast<std::int64_t>(start_y_) - event.y;
            const std::int64_t dx = static_cast<std::int64_t>(start_x_) - event.x;
            const std::int64_t abs_dx = dx < 0 ? -dx : dx;
            if (dy >= swipe_threshold_px_ && abs_dx <= swipe_threshold_px_) {
                return {InputTarget::LockScreen, LockScreenAction::BeginUnlock};
            }
            if (contains(camera_bounds_, event.x, event.y)) {
                return {InputTarget::LockScreen, LockScreenAction::OpenCamera};
            }
            if (contains(flashlight_bounds_, event.x, event.y)) {
                return {InputTarget::LockScreen, LockScreenAction::ToggleFlashlight};
            }
            return {InputTarget::LockScreen, LockScreenAction::None};
        }

        case PointerPhase::Cancel:
            pointer_active_ = false;
            return {InputTarget::LockScreen, LockScreenAction::None};
    }
    return {InputTarget::LockScreen, LockScreenAction::None};
}

}  // namespace fahim::ui
