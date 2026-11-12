#pragma once

#include "fahim/input/InputDevice.hpp"
#include "fahim/security/Authenticator.hpp"
#include "fahim/security/KernelSessionControl.hpp"
#include "fahim/systemui/LockScreenView.hpp"
#include "fahim/ui/Compositor.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fahim::systemui {

enum class SystemScreen : std::uint8_t {
    Home, ControlCenter, Settings, Authenticator, About
};

enum class SystemRadio : std::uint8_t { Wifi, Bluetooth, Airplane, Flashlight };
enum class SystemTheme : std::uint8_t { Dark, Light, Automatic };

struct SystemStatus {
    char time[8]{"--:--"};
    char date[24]{"TIME UNAVAILABLE"};
    char network[20]{"NO SERVICE"};
    std::uint8_t battery_percent{0};
    std::uint64_t unix_time_seconds{0};
    bool time_valid{false};
    bool battery_valid{false};
    bool wifi_enabled{false};
    bool bluetooth_enabled{false};
    bool airplane_enabled{false};
    bool flashlight_enabled{false};
    std::uint8_t brightness_percent{0};
    SystemTheme theme{SystemTheme::Dark};
};

class SystemPlatformServices {
public:
    virtual ~SystemPlatformServices() = default;
    virtual bool read_status(SystemStatus& status) noexcept = 0;
    virtual bool set_radio(SystemRadio radio, bool enabled) noexcept = 0;
    virtual bool set_brightness(std::uint8_t percent) noexcept = 0;
    virtual bool set_theme(SystemTheme theme) noexcept = 0;
};

struct SystemShellState {
    SystemScreen screen{SystemScreen::Home};
    SystemStatus status{};
    bool services_available{false};
    bool vault_available{false};
    bool enrollment_available{false};
    std::array<fahim::security::OneTimeCode, 4> codes{};
    std::size_t code_count{0};
    const char* notice{nullptr};
};

class SystemShellRenderer final {
public:
    SystemShellRenderer(std::uint32_t* argb_pixels, std::uint32_t width,
                        std::uint32_t height, std::size_t buffer_pixels) noexcept;
    bool valid() const noexcept;
    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }
    const std::uint32_t* pixels() const noexcept { return pixels_; }
    bool register_surface(fahim::ui::Compositor& compositor,
                          std::uint32_t surface_id,
                          std::int32_t z_order = 20) const noexcept;
    void render(const SystemShellState& state) noexcept;
    enum class Hit : std::uint8_t {
        None, OpenSettings, OpenAuthenticator, OpenControlCenter, OpenAbout,
        BackHome, ToggleWifi, ToggleBluetooth, ToggleAirplane, ToggleFlashlight,
        BrightnessDown, BrightnessUp, Theme, LockDevice, AddAuthenticator
    };
    Hit hit_test(std::int32_t x, std::int32_t y,
                 SystemScreen screen) const noexcept;

private:
    void fill_rect(fahim::ui::Rect rect, std::uint32_t color) noexcept;
    void round_rect(fahim::ui::Rect rect, std::int32_t radius,
                    std::uint32_t color) noexcept;
    void circle(std::int32_t x, std::int32_t y, std::int32_t radius,
                std::uint32_t color) noexcept;
    void text(std::int32_t x, std::int32_t y, const char* value,
              std::int32_t scale, std::uint32_t color) noexcept;
    void glyph(std::int32_t x, std::int32_t y, char value,
               std::int32_t scale, std::uint32_t color) noexcept;
    static const std::uint8_t* glyph_data(char value) noexcept;
    void base(const SystemShellState& state, const char* title) noexcept;
    void render_home(const SystemShellState& state) noexcept;
    void render_control_center(const SystemShellState& state) noexcept;
    void render_settings(const SystemShellState& state) noexcept;
    void render_authenticator(const SystemShellState& state) noexcept;
    void render_about(const SystemShellState& state) noexcept;

    std::uint32_t* pixels_{nullptr};
    std::uint32_t width_{0};
    std::uint32_t height_{0};
    std::size_t buffer_pixels_{0};
    std::uint32_t panel_color_{0xff172335u};
    std::uint32_t panel_alt_color_{0xff202f43u};
    std::uint32_t text_color_{0xfff5f7fbu};
    std::uint32_t muted_color_{0xff9eafc3u};
    std::uint32_t accent_color_{0xffb6d9ffu};
    std::uint32_t green_color_{0xff74d6a2u};
    std::uint32_t orange_color_{0xffffc779u};
    std::uint32_t red_color_{0xffff8e98u};
    bool light_theme_{false};
};

class SystemShellController final {
public:
    SystemShellController(LockScreenCore& lock_core,
                          LockScreenController& lock_screen,
                          KernelSessionControl& kernel_control,
                          SystemPlatformServices* platform,
                          fahim::security::AuthenticatorVault* vault,
                          SystemShellRenderer& renderer,
                          fahim::ui::Compositor& compositor) noexcept;

    bool handle_event(const fahim::ui::PointerEvent& event) noexcept;
    bool pump_input(fahim::input::TouchInputPipeline& input,
                    std::size_t max_events = 32u) noexcept;
    void tick() noexcept;
    void refresh() noexcept;
    void lock_device() noexcept;
    bool render() noexcept;

    SystemScreen screen() const noexcept { return state_.screen; }
    const SystemShellState& state() const noexcept { return state_; }

private:
    void navigate(SystemScreen screen) noexcept;
    void activate(SystemShellRenderer::Hit hit) noexcept;
    void refresh_authenticator() noexcept;
    void set_notice(const char* notice) noexcept;

    LockScreenCore& lock_core_;
    LockScreenController& lock_screen_;
    KernelSessionControl& kernel_control_;
    SystemPlatformServices* platform_{nullptr};
    fahim::security::AuthenticatorVault* vault_{nullptr};
    SystemShellRenderer& renderer_;
    fahim::ui::Compositor& compositor_;
    SystemShellState state_{};
    std::int32_t gesture_start_y_{0};
    bool pointer_down_{false};
    std::uint64_t surface_id_{0x5359535348454c4cull};
};

}  // namespace fahim::systemui
