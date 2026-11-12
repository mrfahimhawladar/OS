#include "fahim/systemui/SystemShell.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace fahim::systemui {
namespace {
constexpr std::uint32_t C_PANEL = 0xff172335u;
constexpr std::uint32_t C_PANEL_ALT = 0xff202f43u;
constexpr std::uint32_t C_TEXT = 0xfff5f7fbu;
constexpr std::uint32_t C_MUTED = 0xff9eafc3u;
constexpr std::uint32_t C_ACCENT = 0xffb6d9ffu;
constexpr std::uint32_t C_GREEN = 0xff74d6a2u;
constexpr std::uint32_t C_ORANGE = 0xffffc779u;
constexpr std::uint32_t C_RED = 0xffff8e98u;

std::uint32_t scale_dim(std::uint32_t value, float scale) noexcept {
    return static_cast<std::uint32_t>(static_cast<float>(value) * scale);
}
std::int32_t scale_pos(std::int32_t value, float scale) noexcept {
    return static_cast<std::int32_t>(static_cast<float>(value) * scale);
}
}  // namespace

SystemShellRenderer::SystemShellRenderer(std::uint32_t* pixels,
                                         std::uint32_t width,
                                         std::uint32_t height,
                                         std::size_t buffer_pixels) noexcept
    : pixels_(pixels), width_(width), height_(height),
      buffer_pixels_(buffer_pixels) {}

bool SystemShellRenderer::valid() const noexcept {
    const std::uint64_t required = static_cast<std::uint64_t>(width_) * height_;
    return pixels_ != nullptr && width_ >= 240u && height_ >= 480u &&
           width_ <= 4096u && height_ <= 8192u && required <= buffer_pixels_;
}

void SystemShellRenderer::fill_rect(fahim::ui::Rect rect, std::uint32_t color) noexcept {
    if (!valid() || rect.width == 0u || rect.height == 0u) return;
    const std::int64_t left = std::max<std::int64_t>(0, rect.x);
    const std::int64_t top = std::max<std::int64_t>(0, rect.y);
    const std::int64_t right = std::min<std::int64_t>(
        width_, static_cast<std::int64_t>(rect.x) + rect.width);
    const std::int64_t bottom = std::min<std::int64_t>(
        height_, static_cast<std::int64_t>(rect.y) + rect.height);
    for (std::int64_t y = top; y < bottom; ++y) {
        auto* row = pixels_ + static_cast<std::size_t>(y) * width_;
        for (std::int64_t x = left; x < right; ++x)
            row[static_cast<std::size_t>(x)] = color;
    }
}

void SystemShellRenderer::round_rect(fahim::ui::Rect rect, std::int32_t radius,
                                     std::uint32_t color) noexcept {
    if (!valid() || rect.width == 0u || rect.height == 0u) return;
    const std::int64_t r = std::max<std::int64_t>(0,
        std::min<std::int64_t>(radius, std::min(rect.width, rect.height) / 2u));
    if (r == 0) { fill_rect(rect, color); return; }
    const std::int64_t left = std::max<std::int64_t>(0, rect.x);
    const std::int64_t top = std::max<std::int64_t>(0, rect.y);
    const std::int64_t right = std::min<std::int64_t>(
        width_, static_cast<std::int64_t>(rect.x) + rect.width);
    const std::int64_t bottom = std::min<std::int64_t>(
        height_, static_cast<std::int64_t>(rect.y) + rect.height);
    const std::int64_t r2 = r * r;
    const std::int64_t far_x = static_cast<std::int64_t>(rect.x) + rect.width - 1;
    const std::int64_t far_y = static_cast<std::int64_t>(rect.y) + rect.height - 1;
    for (std::int64_t y = top; y < bottom; ++y) {
        auto* row = pixels_ + static_cast<std::size_t>(y) * width_;
        for (std::int64_t x = left; x < right; ++x) {
            const std::int64_t cx = x < rect.x + r ? rect.x + r :
                (x > far_x - r ? far_x - r : x);
            const std::int64_t cy = y < rect.y + r ? rect.y + r :
                (y > far_y - r ? far_y - r : y);
            const std::int64_t dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= r2) row[x] = color;
        }
    }
}

void SystemShellRenderer::circle(std::int32_t cx, std::int32_t cy,
                                 std::int32_t radius, std::uint32_t color) noexcept {
    if (!valid() || radius <= 0) return;
    const std::int64_t r2 = static_cast<std::int64_t>(radius) * radius;
    const std::int64_t left = std::max<std::int64_t>(0, cx - radius);
    const std::int64_t right = std::min<std::int64_t>(width_ - 1u, cx + radius);
    const std::int64_t top = std::max<std::int64_t>(0, cy - radius);
    const std::int64_t bottom = std::min<std::int64_t>(height_ - 1u, cy + radius);
    for (std::int64_t y = top; y <= bottom; ++y) {
        auto* row = pixels_ + static_cast<std::size_t>(y) * width_;
        for (std::int64_t x = left; x <= right; ++x) {
            const std::int64_t dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= r2) row[x] = color;
        }
    }
}

const std::uint8_t* SystemShellRenderer::glyph_data(char value) noexcept {
    static constexpr std::uint8_t blank[5]{0,0,0,0,0};
    static constexpr std::uint8_t font[36][5] = {
        {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
        {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},
        {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
        {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
        {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
        {0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},
        {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
        {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
        {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},
        {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
        {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
    };
    if (value >= '0' && value <= '9') return font[value - '0'];
    if (value >= 'A' && value <= 'Z') return font[10 + value - 'A'];
    if (value >= 'a' && value <= 'z') return font[10 + value - 'a'];
    switch (value) {
        case ':': { static constexpr std::uint8_t g[5]{0,0x36,0x36,0,0}; return g; }
        case '.': { static constexpr std::uint8_t g[5]{0,0x60,0x60,0,0}; return g; }
        case ',': { static constexpr std::uint8_t g[5]{0,0x50,0x30,0,0}; return g; }
        case '-': { static constexpr std::uint8_t g[5]{0x08,0x08,0x08,0x08,0x08}; return g; }
        case '%': { static constexpr std::uint8_t g[5]{0x63,0x13,0x08,0x64,0x63}; return g; }
        case '/': { static constexpr std::uint8_t g[5]{0x20,0x10,0x08,0x04,0x02}; return g; }
        case '\'': { static constexpr std::uint8_t g[5]{0,0x05,0x03,0,0}; return g; }
        case ' ': return blank;
        default: return blank;
    }
}

void SystemShellRenderer::glyph(std::int32_t x, std::int32_t y, char value,
                                std::int32_t scale, std::uint32_t color) noexcept {
    if (scale <= 0) return;
    const auto* data = glyph_data(value);
    for (std::int32_t col = 0; col < 5; ++col)
        for (std::int32_t row = 0; row < 7; ++row)
            if ((data[col] & (1u << row)) != 0u)
                fill_rect({x + col * scale, y + row * scale,
                           static_cast<std::uint32_t>(scale),
                           static_cast<std::uint32_t>(scale)}, color);
}

void SystemShellRenderer::text(std::int32_t x, std::int32_t y,
                               const char* value, std::int32_t scale,
                               std::uint32_t color) noexcept {
    if (value == nullptr || scale <= 0) return;
    for (const char* p = value; *p != '\0'; ++p) {
        glyph(x, y, *p, scale, color);
        x += 6 * scale;
        if (x >= static_cast<std::int32_t>(width_)) break;
    }
}

bool SystemShellRenderer::register_surface(fahim::ui::Compositor& compositor,
                                           std::uint32_t surface_id,
                                           std::int32_t z_order) const noexcept {
    if (!valid() || surface_id == 0u) return false;
    return compositor.upsert_surface(fahim::ui::Surface{
        surface_id, pixels_, width_, height_, width_, 0, 0, z_order,
        fahim::ui::SurfaceRole::Application, true
    });
}

void SystemShellRenderer::base(const SystemShellState& state,
                               const char* title) noexcept {
    const float scale = static_cast<float>(width_) / 440.0f;
    for (std::uint32_t y = 0; y < height_; ++y) {
        const std::uint32_t mix = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(y) * 28u / std::max(1u, height_ - 1u));
        const std::uint32_t color = 0xff000000u |
            ((8u + mix / 3u) << 16u) | ((14u + mix / 2u) << 8u) | (24u + mix);
        auto* row = pixels_ + static_cast<std::size_t>(y) * width_;
        std::fill(row, row + width_, color);
    }
    text(scale_pos(22, scale), scale_pos(23, scale), state.status.time,
         std::max(1, scale_pos(1, scale)), C_TEXT);
    text(scale_pos(310, scale), scale_pos(23, scale),
         state.status.battery_valid ? "BATTERY" : "NO BATTERY",
         std::max(1, scale_pos(1, scale)), C_MUTED);
    round_rect({scale_pos(22, scale), scale_pos(60, scale),
                scale_dim(396, scale), scale_dim(72, scale)},
               scale_pos(22, scale), C_PANEL);
    text(scale_pos(42, scale), scale_pos(83, scale), title, 2, C_TEXT);
    text(scale_pos(42, scale), scale_pos(111, scale),
         state.status.date, 1, C_MUTED);
    if (!state.services_available) {
        text(scale_pos(22, scale), scale_pos(154, scale),
             "SYSTEM SERVICES OFFLINE", 1, C_ORANGE);
    }
    if (state.notice != nullptr) {
        round_rect({scale_pos(22, scale), scale_pos(800, scale),
                    scale_dim(396, scale), scale_dim(54, scale)},
                   scale_pos(16, scale), C_PANEL_ALT);
        text(scale_pos(36, scale), scale_pos(824, scale),
             state.notice, 1, C_ORANGE);
    }
}

void SystemShellRenderer::render_home(const SystemShellState& state) noexcept {
    const float s = static_cast<float>(width_) / 440.0f;
    base(state, "Home");
    text(scale_pos(28, s), scale_pos(177, s), "YOUR SPACE", 1, C_MUTED);

    const char* names[4] = {
        "CONTROL CENTER", "AUTHENTICATOR", "SETTINGS", "ABOUT"
    };
    const std::uint32_t colors[4] = {
        0xff3f78b9u,0xff397cc5u,0xff6e7e96u,0xff526e91u
    };
    for (std::size_t i = 0; i < 4u; ++i) {
        const std::int32_t col = static_cast<std::int32_t>(i % 2u);
        const std::int32_t row = static_cast<std::int32_t>(i / 2u);
        const std::int32_t x = 24 + col * 204;
        const std::int32_t y = 220 + row * 170;
        round_rect({scale_pos(x, s), scale_pos(y, s),
                    scale_dim(188, s), scale_dim(138, s)},
                   scale_pos(24, s), C_PANEL);
        round_rect({scale_pos(x + 14, s), scale_pos(y + 13, s),
                    scale_dim(58, s), scale_dim(58, s)},
                   scale_pos(18, s), colors[i]);
        circle(scale_pos(x + 43, s), scale_pos(y + 42, s),
               scale_pos(13, s), 0x55ffffffu);
        text(scale_pos(x + 14, s), scale_pos(y + 98, s), names[i], 1, C_TEXT);
    }
    round_rect({scale_pos(18, s), scale_pos(615, s),
                scale_dim(404, s), scale_dim(110, s)},
               scale_pos(26, s), 0x99566b82u);
    text(scale_pos(38, s), scale_pos(647, s), "FAHIM OS SYSTEM", 1, C_MUTED);
    text(scale_pos(38, s), scale_pos(678, s),
         state.services_available ? "PLATFORM SERVICES CONNECTED" : "PLATFORM SERVICES OFFLINE",
         1, state.services_available ? C_GREEN : C_ORANGE);
    text(scale_pos(24, s), scale_pos(765, s), "SWIPE DOWN FOR CONTROL CENTER", 1, C_MUTED);
    round_rect({scale_pos(144, s), scale_pos(858, s),
                scale_dim(152, s), scale_dim(6, s)}, scale_pos(3, s), C_TEXT);
    round_rect({scale_pos(18, s), scale_pos(625, s),
                scale_dim(404, s), scale_dim(100, s)},
               scale_pos(30, s), 0x99566b82u);
    text(scale_pos(38, s), scale_pos(655, s), "QUICK ACCESS", 1, C_MUTED);
    text(scale_pos(38, s), scale_pos(685, s), "SETTINGS   AUTHENTICATOR", 1, C_TEXT);
    text(scale_pos(24, s), scale_pos(765, s), "SWIPE DOWN FOR CONTROL CENTER", 1, C_MUTED);
    round_rect({scale_pos(144, s), scale_pos(858, s),
                scale_dim(152, s), scale_dim(6, s)}, scale_pos(3, s), C_TEXT);
}

void SystemShellRenderer::render_control_center(const SystemShellState& state) noexcept {
    const float s = static_cast<float>(width_) / 440.0f;
    base(state, "Control Center");
    text(scale_pos(28, s), scale_pos(177, s), "CONNECTIVITY", 1, C_MUTED);
    const struct Tile { const char* label; bool enabled; std::int32_t x; std::int32_t y; } tiles[] = {
        {"WI-FI", state.status.wifi_enabled, 22, 205},
        {"BLUETOOTH", state.status.bluetooth_enabled, 226, 205},
        {"AIRPLANE", state.status.airplane_enabled, 22, 330},
        {"FLASHLIGHT", state.status.flashlight_enabled, 226, 330}
    };
    for (const auto& tile : tiles) {
        round_rect({scale_pos(tile.x, s), scale_pos(tile.y, s),
                    scale_dim(192, s), scale_dim(106, s)},
                   scale_pos(22, s), tile.enabled ? 0xff345477u : C_PANEL);
        circle(scale_pos(tile.x + 28, s), scale_pos(tile.y + 30, s),
               scale_pos(9, s), tile.enabled ? C_ACCENT : C_MUTED);
        text(scale_pos(tile.x + 17, s), scale_pos(tile.y + 70, s), tile.label, 1, C_TEXT);
        text(scale_pos(tile.x + 144, s), scale_pos(tile.y + 28, s),
             tile.enabled ? "ON" : "OFF", 1, tile.enabled ? C_GREEN : C_MUTED);
    }

    text(scale_pos(28, s), scale_pos(488, s), "DISPLAY BRIGHTNESS", 1, C_MUTED);
    round_rect({scale_pos(22, s), scale_pos(515, s),
                scale_dim(396, s), scale_dim(94, s)}, scale_pos(22, s), C_PANEL);
    round_rect({scale_pos(44, s), scale_pos(555, s),
                scale_dim(352, s), scale_dim(10, s)}, scale_pos(5, s), C_PANEL_ALT);
    if (state.services_available) {
        const auto w = std::max<std::uint32_t>(1u,
            scale_dim(352u * state.status.brightness_percent / 100u, s));
        round_rect({scale_pos(44, s), scale_pos(555, s), w, scale_dim(10, s)},
                   scale_pos(5, s), C_ACCENT);
    }
    text(scale_pos(32, s), scale_pos(578, s),
         state.services_available ? "BRIGHTNESS AVAILABLE" : "BRIGHTNESS SERVICE OFFLINE",
         1, C_MUTED);

    round_rect({scale_pos(22, s), scale_pos(640, s),
                scale_dim(396, s), scale_dim(92, s)}, scale_pos(22, s), C_PANEL);
    text(scale_pos(42, s), scale_pos(673, s), "SYSTEM SETTINGS", 1, C_TEXT);
    text(scale_pos(42, s), scale_pos(703, s), "OPEN SETTINGS TO CONFIGURE DEVICE", 1, C_MUTED);
    text(scale_pos(22, s), scale_pos(770, s), "SWIPE UP TO CLOSE", 1, C_MUTED);
}

void SystemShellRenderer::render_settings(const SystemShellState& state) noexcept {
    const float s = static_cast<float>(width_) / 440.0f;
    base(state, "Settings");
    const char* labels[] = {"WI-FI AND NETWORK", "DISPLAY AND BRIGHTNESS",
                            "APPEARANCE", "SECURITY AND PRIVACY", "ABOUT DEVICE",
                            "LOCK DEVICE"};
    const char* details[] = {
        state.services_available ? "OPEN CONNECTIVITY CONTROLS" : "NETWORK SERVICE OFFLINE",
        state.services_available ? "HARDWARE DISPLAY CONTROLS" : "DISPLAY SERVICE OFFLINE",
        state.status.theme == SystemTheme::Dark ? "DARK APPEARANCE" :
            (state.status.theme == SystemTheme::Light ? "LIGHT APPEARANCE" : "AUTOMATIC"),
        "CREDENTIALS AND AUTHENTICATION", "KERNEL AND SYSTEM INFORMATION",
        "RETURN TO SECURE LOCK SCREEN"
    };
    for (std::size_t i = 0; i < 6u; ++i) {
        const std::int32_t y = 180 + static_cast<std::int32_t>(i) * 96;
        round_rect({scale_pos(22, s), scale_pos(y, s),
                    scale_dim(396, s), scale_dim(78, s)}, scale_pos(17, s), C_PANEL);
        circle(scale_pos(44, s), scale_pos(y + 39, s), scale_pos(8, s),
               i == 5u ? C_RED : C_ACCENT);
        text(scale_pos(66, s), scale_pos(y + 28, s), labels[i], 1, C_TEXT);
        text(scale_pos(66, s), scale_pos(y + 51, s), details[i], 1, C_MUTED);
        text(scale_pos(385, s), scale_pos(y + 39, s), ">", 1, C_MUTED);
    }
}

void SystemShellRenderer::render_authenticator(const SystemShellState& state) noexcept {
    const float s = static_cast<float>(width_) / 440.0f;
    base(state, "Authenticator");
    text(scale_pos(28, s), scale_pos(177, s), "TIME-BASED ONE-TIME CODES", 1, C_MUTED);
    if (!state.vault_available) {
        round_rect({scale_pos(22, s), scale_pos(220, s),
                    scale_dim(396, s), scale_dim(170, s)}, scale_pos(24, s), C_PANEL);
        circle(scale_pos(220, s), scale_pos(270, s), scale_pos(17, s), C_ORANGE);
        text(scale_pos(44, s), scale_pos(320, s), "SECURE VAULT UNAVAILABLE", 1, C_TEXT);
        text(scale_pos(44, s), scale_pos(348, s), "NO ACCOUNT DATA IS SHOWN", 1, C_MUTED);
    } else if (state.code_count == 0u) {
        round_rect({scale_pos(22, s), scale_pos(220, s),
                    scale_dim(396, s), scale_dim(170, s)}, scale_pos(24, s), C_PANEL);
        text(scale_pos(44, s), scale_pos(274, s), "NO ACCOUNTS ENROLLED", 1, C_TEXT);
        text(scale_pos(44, s), scale_pos(306, s), "ADD AN ACCOUNT USING SECURE UI", 1, C_MUTED);
    } else {
        for (std::size_t i = 0; i < state.code_count; ++i) {
            const std::int32_t y = 205 + static_cast<std::int32_t>(i) * 133;
            round_rect({scale_pos(22, s), scale_pos(y, s),
                        scale_dim(396, s), scale_dim(112, s)}, scale_pos(20, s), C_PANEL);
            text(scale_pos(42, s), scale_pos(y + 25, s),
                 state.codes[i].issuer, 1, C_MUTED);
            text(scale_pos(42, s), scale_pos(y + 57, s),
                 state.codes[i].label, 2, C_TEXT);
            text(scale_pos(270, s), scale_pos(y + 57, s),
                 state.codes[i].code, 2, C_ACCENT);
            text(scale_pos(42, s), scale_pos(y + 83, s),
                 "EXPIRES IN", 1, C_MUTED);
            text(scale_pos(135, s), scale_pos(y + 83, s),
                 state.codes[i].seconds_remaining < 10u ? "0" : "", 1, C_ORANGE);
        }
    }
    round_rect({scale_pos(22, s), scale_pos(745, s),
                scale_dim(396, s), scale_dim(66, s)}, scale_pos(18, s), C_PANEL_ALT);
    text(scale_pos(42, s), scale_pos(773, s), "ADD ACCOUNT / SECURE ENROLLMENT", 1, C_TEXT);
    if (!state.enrollment_available)
        text(scale_pos(42, s), scale_pos(829, s), "TRUSTED PROVISIONING SERVICE OFFLINE", 1, C_ORANGE);
}

void SystemShellRenderer::render_about(const SystemShellState& state) noexcept {
    const float s = static_cast<float>(width_) / 440.0f;
    base(state, "About This Device");
    const char* labels[] = {"OPERATING SYSTEM", "KERNEL", "DISPLAY", "INPUT", "SECURE CREDENTIALS"};
    const char* values[] = {"FAHIM OS MOBILE", "ARM64 / EL1 + EL0", "PLATFORM DRIVER REQUIRED",
                            "TOUCH DRIVER REQUIRED", "OP-TEE PLATFORM REQUIRED"};
    for (std::size_t i = 0; i < 5u; ++i) {
        const std::int32_t y = 205 + static_cast<std::int32_t>(i) * 105;
        round_rect({scale_pos(22, s), scale_pos(y, s),
                    scale_dim(396, s), scale_dim(82, s)}, scale_pos(18, s), C_PANEL);
        text(scale_pos(40, s), scale_pos(y + 26, s), labels[i], 1, C_MUTED);
        text(scale_pos(40, s), scale_pos(y + 53, s), values[i], 1, C_TEXT);
    }
    text(scale_pos(24, s), scale_pos(770, s), "HARDWARE CAPABILITIES ARE REPORTED BY DRIVERS", 1, C_MUTED);
}

void SystemShellRenderer::render(const SystemShellState& state) noexcept {
    if (!valid()) return;
    switch (state.screen) {
        case SystemScreen::Home: render_home(state); break;
        case SystemScreen::ControlCenter: render_control_center(state); break;
        case SystemScreen::Settings: render_settings(state); break;
        case SystemScreen::Authenticator: render_authenticator(state); break;
        case SystemScreen::About: render_about(state); break;
    }
}

SystemShellRenderer::Hit SystemShellRenderer::hit_test(
    std::int32_t x, std::int32_t y, SystemScreen screen) const noexcept {
    if (!valid()) return Hit::None;
    const float s = static_cast<float>(width_) / 440.0f;
    const auto in = [x,y,s](int left,int top,int right,int bottom) noexcept {
        return x >= scale_pos(left,s) && x < scale_pos(right,s) &&
               y >= scale_pos(top,s) && y < scale_pos(bottom,s);
    };
    if (screen != SystemScreen::Home && in(18, 56, 115, 135)) return Hit::BackHome;

    switch (screen) {
        case SystemScreen::Home:
            if (in(24, 220, 212, 358)) return Hit::OpenControlCenter;
            if (in(228, 220, 416, 358)) return Hit::OpenAuthenticator;
            if (in(24, 390, 212, 528)) return Hit::OpenSettings;
            if (in(228, 390, 416, 528)) return Hit::OpenAbout;
            return Hit::None;
        case SystemScreen::ControlCenter:
            if (in(22,205,214,311)) return Hit::ToggleWifi;
            if (in(226,205,418,311)) return Hit::ToggleBluetooth;
            if (in(22,330,214,436)) return Hit::ToggleAirplane;
            if (in(226,330,418,436)) return Hit::ToggleFlashlight;
            if (in(22,515,418,610)) {
                if (x < static_cast<std::int32_t>(width_ / 2u)) return Hit::BrightnessDown;
                return Hit::BrightnessUp;
            }
            if (in(22,640,418,732)) return Hit::OpenSettings;
            return Hit::None;
        case SystemScreen::Settings:
            if (in(22,180,418,258)) return Hit::OpenControlCenter;
            if (in(22,276,418,354)) return Hit::OpenControlCenter;
            if (in(22,372,418,450)) return Hit::Theme;
            if (in(22,468,418,546)) return Hit::OpenAuthenticator;
            if (in(22,564,418,642)) return Hit::OpenAbout;
            if (in(22,660,418,738)) return Hit::LockDevice;
            return Hit::None;
        case SystemScreen::Authenticator:
            if (in(22,745,418,812)) return Hit::AddAuthenticator;
            return Hit::None;
        case SystemScreen::About:
            return Hit::None;
    }
    return Hit::None;
}

}  // namespace fahim::systemui
