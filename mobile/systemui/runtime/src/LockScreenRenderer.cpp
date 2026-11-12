#include "fahim/systemui/LockScreenView.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace fahim::systemui {

namespace {
constexpr std::uint32_t kTransparent = 0x00000000u;
constexpr std::uint32_t kWhite = 0xfff7f8fcu;
constexpr std::uint32_t kMuted = 0xffc5cbd5u;
constexpr std::uint32_t kCard = 0xd91b2330u;
constexpr std::uint32_t kCardBorder = 0x456f8198u;
constexpr std::uint32_t kAccent = 0xffa9d4ffu;
constexpr std::uint32_t kDark = 0xff111b2au;
constexpr std::uint32_t kDanger = 0xffff9e9eu;

bool point_in_rect(std::int32_t x, std::int32_t y,
                   const fahim::ui::Rect& rect) noexcept {
    return rect.width != 0u && rect.height != 0u &&
        x >= rect.x && y >= rect.y &&
        static_cast<std::int64_t>(x) <
            static_cast<std::int64_t>(rect.x) + rect.width &&
        static_cast<std::int64_t>(y) <
            static_cast<std::int64_t>(rect.y) + rect.height;
}
}  // namespace

LockScreenRenderer::LockScreenRenderer(std::uint32_t* argb_pixels,
                                       std::uint32_t width,
                                       std::uint32_t height,
                                       std::size_t buffer_pixels) noexcept
    : pixels_(argb_pixels), width_(width), height_(height),
      buffer_pixels_(buffer_pixels) {}

bool LockScreenRenderer::valid() const noexcept {
    const std::uint64_t required = static_cast<std::uint64_t>(width_) * height_;
    return pixels_ != nullptr && width_ >= 180u && height_ >= 320u &&
           width_ <= 4096u && height_ <= 8192u &&
           required <= buffer_pixels_;
}

void LockScreenRenderer::clear(std::uint32_t color) noexcept {
    const std::size_t count = static_cast<std::size_t>(width_) * height_;
    for (std::size_t i = 0; i < count; ++i) pixels_[i] = color;
}

void LockScreenRenderer::fill_rect(fahim::ui::Rect rect,
                                   std::uint32_t color) noexcept {
    if (!valid() || rect.width == 0u || rect.height == 0u) return;
    const std::int64_t left = std::max<std::int64_t>(0, rect.x);
    const std::int64_t top = std::max<std::int64_t>(0, rect.y);
    const std::int64_t right = std::min<std::int64_t>(
        width_, static_cast<std::int64_t>(rect.x) + rect.width);
    const std::int64_t bottom = std::min<std::int64_t>(
        height_, static_cast<std::int64_t>(rect.y) + rect.height);
    if (left >= right || top >= bottom) return;
    for (std::int64_t y = top; y < bottom; ++y) {
        auto* row = pixels_ + static_cast<std::size_t>(y) * width_;
        for (std::int64_t x = left; x < right; ++x) {
            row[static_cast<std::size_t>(x)] = color;
        }
    }
}

void LockScreenRenderer::fill_rounded_rect(fahim::ui::Rect rect,
                                           std::int32_t radius,
                                           std::uint32_t color) noexcept {
    if (!valid() || rect.width == 0u || rect.height == 0u) return;
    const std::int32_t safe_radius = std::max<std::int32_t>(
        0, std::min<std::int32_t>(radius,
        static_cast<std::int32_t>(std::min(rect.width, rect.height) / 2u)));
    if (safe_radius == 0) {
        fill_rect(rect, color);
        return;
    }
    const std::int64_t left = std::max<std::int64_t>(0, rect.x);
    const std::int64_t top = std::max<std::int64_t>(0, rect.y);
    const std::int64_t right = std::min<std::int64_t>(
        width_, static_cast<std::int64_t>(rect.x) + rect.width);
    const std::int64_t bottom = std::min<std::int64_t>(
        height_, static_cast<std::int64_t>(rect.y) + rect.height);
    const std::int64_t r = safe_radius;
    const std::int64_t local_right = static_cast<std::int64_t>(rect.x) + rect.width - 1;
    const std::int64_t local_bottom = static_cast<std::int64_t>(rect.y) + rect.height - 1;
    for (std::int64_t y = top; y < bottom; ++y) {
        auto* row = pixels_ + static_cast<std::size_t>(y) * width_;
        for (std::int64_t x = left; x < right; ++x) {
            std::int64_t cx = x;
            std::int64_t cy = y;
            if (x < static_cast<std::int64_t>(rect.x) + r) {
                cx = static_cast<std::int64_t>(rect.x) + r;
            } else if (x > local_right - r) {
                cx = local_right - r;
            }
            if (y < static_cast<std::int64_t>(rect.y) + r) {
                cy = static_cast<std::int64_t>(rect.y) + r;
            } else if (y > local_bottom - r) {
                cy = local_bottom - r;
            }
            const std::int64_t dx = x - cx;
            const std::int64_t dy = y - cy;
            if (dx * dx + dy * dy <= r * r) {
                row[static_cast<std::size_t>(x)] = color;
            }
        }
    }
}

void LockScreenRenderer::fill_circle(std::int32_t cx, std::int32_t cy,
                                     std::int32_t radius,
                                     std::uint32_t color) noexcept {
    if (!valid() || radius <= 0) return;
    const std::int64_t r2 = static_cast<std::int64_t>(radius) * radius;
    const std::int64_t top = std::max<std::int64_t>(0, cy - radius);
    const std::int64_t bottom = std::min<std::int64_t>(height_ - 1u, cy + radius);
    const std::int64_t left = std::max<std::int64_t>(0, cx - radius);
    const std::int64_t right = std::min<std::int64_t>(width_ - 1u, cx + radius);
    for (std::int64_t y = top; y <= bottom; ++y) {
        auto* row = pixels_ + static_cast<std::size_t>(y) * width_;
        for (std::int64_t x = left; x <= right; ++x) {
            const std::int64_t dx = x - cx;
            const std::int64_t dy = y - cy;
            if (dx * dx + dy * dy <= r2) row[static_cast<std::size_t>(x)] = color;
        }
    }
}

const std::uint8_t* LockScreenRenderer::glyph(char value) noexcept {
    static constexpr std::uint8_t blank[5]{0, 0, 0, 0, 0};
    // Five vertical columns, least-significant bit at the top.
    static constexpr std::uint8_t font[36][5] = {
        {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
        {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e},
        {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
        {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
        {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
        {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
        {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
        {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
        {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
        {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
        {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
    };
    if (value >= '0' && value <= '9') return font[value - '0'];
    if (value >= 'A' && value <= 'Z') return font[10 + value - 'A'];
    if (value >= 'a' && value <= 'z') return font[10 + value - 'a'];
    switch (value) {
        case ':': { static constexpr std::uint8_t g[5]{0,0x36,0x36,0,0}; return g; }
        case ',': { static constexpr std::uint8_t g[5]{0,0x50,0x30,0,0}; return g; }
        case '.': { static constexpr std::uint8_t g[5]{0,0x60,0x60,0,0}; return g; }
        case '-': { static constexpr std::uint8_t g[5]{0x08,0x08,0x08,0x08,0x08}; return g; }
        case '%': { static constexpr std::uint8_t g[5]{0x63,0x13,0x08,0x64,0x63}; return g; }
        case '/': { static constexpr std::uint8_t g[5]{0x20,0x10,0x08,0x04,0x02}; return g; }
        case ' ': return blank;
        default: return blank;
    }
}

void LockScreenRenderer::draw_glyph(std::int32_t x, std::int32_t y,
                                    char value, std::int32_t scale,
                                    std::uint32_t color) noexcept {
    if (scale <= 0) return;
    const std::uint8_t* columns = glyph(value);
    for (std::int32_t col = 0; col < 5; ++col) {
        for (std::int32_t row = 0; row < 7; ++row) {
            if ((columns[col] & (1u << row)) != 0u) {
                fill_rect(fahim::ui::Rect{
                    x + col * scale, y + row * scale,
                    static_cast<std::uint32_t>(scale),
                    static_cast<std::uint32_t>(scale)}, color);
            }
        }
    }
}

void LockScreenRenderer::draw_text(std::int32_t x, std::int32_t y,
                                   const char* text, std::int32_t scale,
                                   std::uint32_t color) noexcept {
    if (text == nullptr || scale <= 0) return;
    std::int32_t cursor = x;
    for (const char* p = text; *p != '\0'; ++p) {
        draw_glyph(cursor, y, *p, scale, color);
        cursor += 6 * scale;
        if (cursor >= static_cast<std::int32_t>(width_)) break;
    }
}

fahim::ui::Rect LockScreenRenderer::camera_shortcut_bounds() const noexcept {
    const std::uint32_t size = std::max<std::uint32_t>(48u, width_ / 8u);
    return {static_cast<std::int32_t>(width_ - size - 26u),
            static_cast<std::int32_t>(height_ - size - 30u), size, size};
}

fahim::ui::Rect LockScreenRenderer::flashlight_shortcut_bounds() const noexcept {
    const std::uint32_t size = std::max<std::uint32_t>(48u, width_ / 8u);
    return {26, static_cast<std::int32_t>(height_ - size - 30u), size, size};
}

PinHit LockScreenRenderer::hit_test_pin(std::int32_t x,
                                        std::int32_t y) const noexcept {
    if (!valid()) return PinHit::None;
    const std::int32_t margin = 24;
    const std::int32_t area_width = static_cast<std::int32_t>(width_) - 2 * margin;
    const std::int32_t cell_width = area_width / 3;
    const std::int32_t row_height = std::max<std::int32_t>(50,
        static_cast<std::int32_t>(height_ / 13u));
    const std::int32_t keypad_y = static_cast<std::int32_t>(height_ * 43u / 100u);
    if (y >= keypad_y && y < keypad_y + row_height * 4 &&
        x >= margin && x < margin + area_width) {
        const std::int32_t col = (x - margin) / cell_width;
        const std::int32_t row = (y - keypad_y) / row_height;
        static constexpr PinHit keys[4][3] = {
            {PinHit::Digit1, PinHit::Digit2, PinHit::Digit3},
            {PinHit::Digit4, PinHit::Digit5, PinHit::Digit6},
            {PinHit::Digit7, PinHit::Digit8, PinHit::Digit9},
            {PinHit::Cancel, PinHit::Digit0, PinHit::Delete}
        };
        return keys[row][col];
    }
    const std::int32_t submit_y = keypad_y + row_height * 4 + 12;
    if (y >= submit_y && y < submit_y + 50 &&
        x >= margin && x < margin + area_width) {
        return PinHit::Submit;
    }
    return PinHit::None;
}

void LockScreenRenderer::render(const LockScreenFrameInfo& frame,
                                const LockScreenViewState& state) noexcept {
    if (!valid()) return;
    clear(kTransparent);

    // Vertical midnight-blue gradient background.
    for (std::uint32_t y = 0; y < height_; ++y) {
        const std::uint32_t mix = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(y) * 255u) / std::max(1u, height_ - 1u));
        const std::uint32_t red = 17u + (mix * 7u / 255u);
        const std::uint32_t green = 29u + (mix * 13u / 255u);
        const std::uint32_t blue = 48u + (mix * 24u / 255u);
        const std::uint32_t color = 0xff000000u |
            (red << 16u) | (green << 8u) | blue;
        auto* row = pixels_ + static_cast<std::size_t>(y) * width_;
        for (std::uint32_t x = 0; x < width_; ++x) row[x] = color;
    }

    if (!state.bouncer_open) {
        draw_text(24, 22, frame.network, 1, kMuted);
        draw_text(static_cast<std::int32_t>(width_) - 72, 22, frame.battery, 1, kMuted);

        const std::int32_t clock_scale = std::max<std::int32_t>(
            5, std::min<std::int32_t>(10, static_cast<std::int32_t>(width_ / 42u)));
        const std::int32_t clock_width = static_cast<std::int32_t>(
            std::strlen(frame.time == nullptr ? "" : frame.time)) * 6 * clock_scale;
        draw_text((static_cast<std::int32_t>(width_) - clock_width) / 2,
                  static_cast<std::int32_t>(height_ / 8u),
                  frame.time, clock_scale, kWhite);
        const std::int32_t date_width = static_cast<std::int32_t>(
            std::strlen(frame.date == nullptr ? "" : frame.date)) * 12;
        draw_text((static_cast<std::int32_t>(width_) - date_width) / 2,
                  static_cast<std::int32_t>(height_ / 8u) + clock_scale * 9 + 12,
                  frame.date, 2, kMuted);

        const std::int32_t card_x = 22;
        const std::int32_t card_width = static_cast<std::int32_t>(width_) - 44;
        const std::int32_t card_height = std::max<std::int32_t>(70,
            static_cast<std::int32_t>(height_ / 11u));
        const std::int32_t cards_y = static_cast<std::int32_t>(height_ * 52u / 100u);
        for (std::int32_t i = 0; i < 2; ++i) {
            const std::int32_t top = cards_y + i * (card_height + 12);
            fill_rounded_rect({card_x, top,
                               static_cast<std::uint32_t>(card_width),
                               static_cast<std::uint32_t>(card_height)}, 18, kCard);
            fill_circle(card_x + 23, top + card_height / 2, 12, kAccent);
            draw_text(card_x + 16, top + card_height / 2 - 5, i == 0 ? "M" : "C", 1, kDark);
            draw_text(card_x + 48, top + 14,
                      i == 0 ? frame.notification_title : "CALENDAR", 1, kWhite);
            draw_text(card_x + 48, top + 36,
                      i == 0 ? frame.notification_body : "Your reminders appear here",
                      1, kMuted);
        }

        const fahim::ui::Rect flashlight = flashlight_shortcut_bounds();
        const fahim::ui::Rect camera = camera_shortcut_bounds();
        fill_circle(flashlight.x + static_cast<std::int32_t>(flashlight.width / 2u),
                    flashlight.y + static_cast<std::int32_t>(flashlight.height / 2u),
                    static_cast<std::int32_t>(flashlight.width / 2u),
                    state.flashlight_on ? kAccent : 0x99515f72u);
        fill_circle(camera.x + static_cast<std::int32_t>(camera.width / 2u),
                    camera.y + static_cast<std::int32_t>(camera.height / 2u),
                    static_cast<std::int32_t>(camera.width / 2u), 0x99515f72u);
        draw_text(flashlight.x + 12, flashlight.y + 19, "F", 2,
                  state.flashlight_on ? kDark : kWhite);
        draw_text(camera.x + 12, camera.y + 19, "C", 2, kWhite);
        if (state.message != nullptr) {
            draw_text(24, static_cast<std::int32_t>(height_) - 96,
                      state.message, 1, kMuted);
        }
        draw_text((static_cast<std::int32_t>(width_) - 108) / 2,
                  static_cast<std::int32_t>(height_) - 38,
                  "SWIPE UP", 1, kWhite);
        fill_rounded_rect({static_cast<std::int32_t>((width_ - 110u) / 2u),
                           static_cast<std::int32_t>(height_) - 13, 110u, 4u},
                          2, kWhite);
        return;
    }

    // PIN bouncer overlays the lock screen and owns all input until cancelled.
    fill_rect({0, 0, width_, height_}, 0xc90b1421u);
    draw_text(28, 30, frame.time, 2, kMuted);
    draw_text(28, static_cast<std::int32_t>(height_ / 7u),
              "ENTER PIN", 3, kWhite);
    draw_text(28, static_cast<std::int32_t>(height_ / 7u) + 30,
              "USE YOUR DEVICE CODE", 1, kMuted);

    const std::int32_t dot_y = static_cast<std::int32_t>(height_ * 32u / 100u);
    const std::int32_t dot_gap = 25;
    const std::int32_t dots_width = 6 * dot_gap;
    const std::int32_t dot_start = (static_cast<std::int32_t>(width_) - dots_width) / 2;
    for (std::size_t i = 0; i < 6u; ++i) {
        fill_circle(dot_start + static_cast<std::int32_t>(i) * dot_gap,
                    dot_y, 5, i < state.pin_digits ? kWhite : 0xff637184u);
    }

    const std::int32_t margin = 24;
    const std::int32_t area_width = static_cast<std::int32_t>(width_) - 2 * margin;
    const std::int32_t cell_width = area_width / 3;
    const std::int32_t row_height = std::max<std::int32_t>(50,
        static_cast<std::int32_t>(height_ / 13u));
    const std::int32_t keypad_y = static_cast<std::int32_t>(height_ * 43u / 100u);
    static constexpr const char* labels[4][3] = {
        {"1","2","3"}, {"4","5","6"}, {"7","8","9"}, {"CANCEL","0","DEL"}
    };
    for (std::int32_t row = 0; row < 4; ++row) {
        for (std::int32_t col = 0; col < 3; ++col) {
            const std::int32_t x = margin + col * cell_width;
            const std::int32_t y = keypad_y + row * row_height;
            fill_rounded_rect({x + 4, y + 3,
                               static_cast<std::uint32_t>(cell_width - 8),
                               static_cast<std::uint32_t>(row_height - 6)},
                              16, 0x665c6d80u);
            const char* label = labels[row][col];
            const std::int32_t scale = (row == 3 && col != 1) ? 1 : 3;
            const std::int32_t text_width = static_cast<std::int32_t>(
                std::strlen(label)) * 6 * scale;
            draw_text(x + (cell_width - text_width) / 2,
                      y + (row_height - 7 * scale) / 2,
                      label, scale, kWhite);
        }
    }
    const std::int32_t submit_y = keypad_y + row_height * 4 + 12;
    fill_rounded_rect({margin, submit_y, static_cast<std::uint32_t>(area_width), 46u},
                      19, kAccent);
    draw_text(static_cast<std::int32_t>(width_) / 2 - 30, submit_y + 16,
              "UNLOCK", 1, kDark);

    if (state.locked_out) {
        draw_text(28, static_cast<std::int32_t>(height_) / 7u + 54,
                  "TRY AGAIN LATER", 1, kDanger);
    } else if (state.message != nullptr) {
        draw_text(28, static_cast<std::int32_t>(height_) / 7u + 54,
                  state.message, 1, kDanger);
    } else if (state.lockout_seconds != 0u) {
        draw_text(28, static_cast<std::int32_t>(height_) / 7u + 54,
                  "LOCKED", 1, kDanger);
    }
}

}  // namespace fahim::systemui
