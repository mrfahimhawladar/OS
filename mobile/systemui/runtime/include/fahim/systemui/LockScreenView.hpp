#pragma once

#include "fahim/security/KernelSessionControl.hpp"
#include "fahim/ui/Display.hpp"
#include "fahim/ui/Compositor.hpp"
#include "fahim/ui/InputRouter.hpp"
#include "LockScreenCore.hpp"

#include <cstddef>
#include <cstdint>

namespace fahim::systemui {

enum class PinHit : std::uint8_t {
    None, Digit0, Digit1, Digit2, Digit3, Digit4, Digit5,
    Digit6, Digit7, Digit8, Digit9, Delete, Submit, Cancel
};

struct LockScreenFrameInfo {
    const char* time{"7:12"};
    const char* date{"Thu, Nov 12"};
    const char* network{"WIFI"};
    const char* battery{"86%"};
    const char* notification_title{"MESSAGES"};
    const char* notification_body{"Your notifications appear here"};
};

struct LockScreenViewState {
    std::size_t pin_digits{0};
    bool bouncer_open{false};
    bool flashlight_on{false};
    bool camera_preview{false};
    bool locked_out{false};
    std::uint32_t lockout_seconds{0};
    const char* message{nullptr};
};

class LockScreenRenderer final {
public:
    LockScreenRenderer(std::uint32_t* argb_pixels,
                       std::uint32_t width,
                       std::uint32_t height,
                       std::size_t buffer_pixels) noexcept;

    bool valid() const noexcept;
    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }
    std::uint32_t* pixels() noexcept { return pixels_; }
    const std::uint32_t* pixels() const noexcept { return pixels_; }
    std::uint32_t stride_pixels() const noexcept { return width_; }

    fahim::ui::Rect camera_shortcut_bounds() const noexcept;
    fahim::ui::Rect flashlight_shortcut_bounds() const noexcept;
    PinHit hit_test_pin(std::int32_t x, std::int32_t y) const noexcept;

    void render(const LockScreenFrameInfo& frame,
                const LockScreenViewState& state) noexcept;
    bool register_surface(fahim::ui::Compositor& compositor,
                          std::uint32_t surface_id,
                          std::int32_t z_order = 0) const noexcept;

private:
    void clear(std::uint32_t color) noexcept;
    void fill_rect(fahim::ui::Rect rect, std::uint32_t color) noexcept;
    void fill_rounded_rect(fahim::ui::Rect rect, std::int32_t radius,
                           std::uint32_t color) noexcept;
    void fill_circle(std::int32_t cx, std::int32_t cy,
                     std::int32_t radius, std::uint32_t color) noexcept;
    void draw_text(std::int32_t x, std::int32_t y, const char* text,
                   std::int32_t scale, std::uint32_t color) noexcept;
    void draw_glyph(std::int32_t x, std::int32_t y, char value,
                    std::int32_t scale, std::uint32_t color) noexcept;
    static const std::uint8_t* glyph(char value) noexcept;

    std::uint32_t* pixels_{nullptr};
    std::uint32_t width_{0};
    std::uint32_t height_{0};
    std::size_t buffer_pixels_{0};
};

class LockScreenController final {
public:
    LockScreenController(LockScreenCore& core,
                         KernelSessionControl& kernel_control,
                         LockScreenRenderer& renderer) noexcept;

    // Feed normalized touchscreen events from TouchInputPipeline.
    // Returns false only when the lock-screen view is no longer active.
    bool handle_event(const fahim::ui::PointerEvent& event) noexcept;
    void tick() noexcept;
    void lock() noexcept;
    void render(const LockScreenFrameInfo& frame) noexcept;
    bool render_to_compositor(const LockScreenFrameInfo& frame,
                              fahim::ui::Compositor& compositor,
                              std::uint32_t surface_id,
                              std::int32_t z_order = 0) noexcept;

    bool bouncer_open() const noexcept { return bouncer_open_; }
    bool flashlight_on() const noexcept { return flashlight_on_; }
    bool camera_preview_open() const noexcept { return camera_preview_open_; }
    const char* status_message() const noexcept { return message_; }

private:
    void submit_pin() noexcept;
    void apply_lock_state_to_kernel() noexcept;
    void set_message(const char* message) noexcept;

    LockScreenCore& core_;
    KernelSessionControl& kernel_control_;
    LockScreenRenderer& renderer_;
    fahim::ui::LockScreenInputRouter input_router_;
    bool bouncer_open_{false};
    bool flashlight_on_{false};
    bool camera_preview_open_{false};
    bool was_lockout_{false};
    const char* message_{nullptr};
};

}  // namespace fahim::systemui
