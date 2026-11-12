#pragma once

#include "fahim/ui/Display.hpp"

#include <cstdint>

namespace fahim::ui {

enum class PointerPhase : std::uint8_t { Down, Move, Up, Cancel };
enum class InputTarget : std::uint8_t { None, LockScreen, Application };
enum class LockScreenAction : std::uint8_t {
    None, BeginUnlock, OpenCamera, ToggleFlashlight
};

struct PointerEvent {
    PointerPhase phase{PointerPhase::Move};
    std::int32_t x{0};
    std::int32_t y{0};
    std::uint64_t timestamp_ms{0};
};

struct InputDecision {
    InputTarget target{InputTarget::None};
    LockScreenAction action{LockScreenAction::None};
};

// Hardware drivers normalize touch/controller events into PointerEvent. While
// locked, this router never returns Application as the target.
class LockScreenInputRouter final {
public:
    explicit LockScreenInputRouter(std::uint32_t swipe_threshold_px = 72u) noexcept;

    void set_locked(bool locked) noexcept;
    void set_camera_bounds(Rect bounds) noexcept { camera_bounds_ = bounds; }
    void set_flashlight_bounds(Rect bounds) noexcept { flashlight_bounds_ = bounds; }
    InputDecision route(const PointerEvent& event) noexcept;

private:
    static bool contains(Rect rect, std::int32_t x, std::int32_t y) noexcept;

    std::uint32_t swipe_threshold_px_{72u};
    bool locked_{true};
    bool pointer_active_{false};
    std::int32_t start_x_{0};
    std::int32_t start_y_{0};
    Rect camera_bounds_{};
    Rect flashlight_bounds_{};
};

}  // namespace fahim::ui
