#include "fahim/ui/Display.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace fahim::ui {

FramebufferDisplay::FramebufferDisplay(FramebufferConfig config,
                                       FlushCallback flush,
                                       void* flush_context) noexcept
    : config_(config), flush_(flush), flush_context_(flush_context) {
    const std::uint32_t bpp = bytes_per_pixel();
    if (config_.memory == nullptr || config_.width == 0u || config_.height == 0u ||
        config_.width > 16384u || config_.height > 16384u || bpp == 0u) return;

    const std::uint64_t row_bytes = static_cast<std::uint64_t>(config_.width) * bpp;
    const std::uint64_t required = static_cast<std::uint64_t>(config_.stride_bytes) *
                                   config_.height;
    if (config_.stride_bytes < row_bytes ||
        required > config_.memory_bytes ||
        required > std::numeric_limits<std::size_t>::max()) return;
    valid_ = true;
}

std::uint32_t FramebufferDisplay::bytes_per_pixel() const noexcept {
    switch (config_.format) {
        case PixelFormat::XRGB8888: return 4u;
        case PixelFormat::RGB565: return 2u;
        default: return 0u;
    }
}

void FramebufferDisplay::write_pixel(std::uint32_t x, std::uint32_t y,
                                     Color color) noexcept {
    auto* row = reinterpret_cast<volatile std::uint8_t*>(config_.memory) +
                static_cast<std::size_t>(y) * config_.stride_bytes;
    if (config_.format == PixelFormat::XRGB8888) {
        const std::uint32_t packed = 0xff000000u |
            (static_cast<std::uint32_t>(color.red) << 16u) |
            (static_cast<std::uint32_t>(color.green) << 8u) |
            static_cast<std::uint32_t>(color.blue);
        reinterpret_cast<volatile std::uint32_t*>(row)[x] = packed;
    } else {
        const std::uint16_t packed = static_cast<std::uint16_t>(
            ((static_cast<std::uint16_t>(color.red) & 0xf8u) << 8u) |
            ((static_cast<std::uint16_t>(color.green) & 0xfcu) << 3u) |
            (static_cast<std::uint16_t>(color.blue) >> 3u));
        reinterpret_cast<volatile std::uint16_t*>(row)[x] = packed;
    }
}

void FramebufferDisplay::clear(Color color) noexcept {
    if (!valid_) return;
    for (std::uint32_t y = 0; y < config_.height; ++y) {
        for (std::uint32_t x = 0; x < config_.width; ++x) write_pixel(x, y, color);
    }
    damage_ = Rect{0, 0, config_.width, config_.height};
    has_damage_ = true;
}

bool FramebufferDisplay::draw_pixel(std::int32_t x, std::int32_t y,
                                    Color color) noexcept {
    if (!valid_ || x < 0 || y < 0 ||
        static_cast<std::uint32_t>(x) >= config_.width ||
        static_cast<std::uint32_t>(y) >= config_.height) return false;

    const auto px = static_cast<std::uint32_t>(x);
    const auto py = static_cast<std::uint32_t>(y);
    write_pixel(px, py, color);

    if (!has_damage_) {
        damage_ = Rect{ x, y, 1u, 1u };
        has_damage_ = true;
    } else {
        const std::int64_t left = std::min<std::int64_t>(damage_.x, x);
        const std::int64_t top = std::min<std::int64_t>(damage_.y, y);
        const std::int64_t right = std::max<std::int64_t>(
            static_cast<std::int64_t>(damage_.x) + damage_.width,
            static_cast<std::int64_t>(x) + 1);
        const std::int64_t bottom = std::max<std::int64_t>(
            static_cast<std::int64_t>(damage_.y) + damage_.height,
            static_cast<std::int64_t>(y) + 1);
        damage_ = Rect{static_cast<std::int32_t>(left),
                       static_cast<std::int32_t>(top),
                       static_cast<std::uint32_t>(right - left),
                       static_cast<std::uint32_t>(bottom - top)};
    }
    return true;
}

bool FramebufferDisplay::fill_rect(Rect rect, Color color) noexcept {
    if (!valid_ || rect.width == 0u || rect.height == 0u) return false;
    const std::int64_t x0 = std::max<std::int64_t>(0, rect.x);
    const std::int64_t y0 = std::max<std::int64_t>(0, rect.y);
    const std::int64_t x1 = std::min<std::int64_t>(
        config_.width, static_cast<std::int64_t>(rect.x) + rect.width);
    const std::int64_t y1 = std::min<std::int64_t>(
        config_.height, static_cast<std::int64_t>(rect.y) + rect.height);
    if (x0 >= x1 || y0 >= y1) return false;

    for (std::int64_t y = y0; y < y1; ++y) {
        for (std::int64_t x = x0; x < x1; ++x) {
            write_pixel(static_cast<std::uint32_t>(x),
                        static_cast<std::uint32_t>(y), color);
        }
    }

    const Rect clipped{static_cast<std::int32_t>(x0),
                       static_cast<std::int32_t>(y0),
                       static_cast<std::uint32_t>(x1 - x0),
                       static_cast<std::uint32_t>(y1 - y0)};
    if (!has_damage_) {
        damage_ = clipped;
        has_damage_ = true;
    } else {
        const std::int64_t left = std::min<std::int64_t>(damage_.x, clipped.x);
        const std::int64_t top = std::min<std::int64_t>(damage_.y, clipped.y);
        const std::int64_t right = std::max<std::int64_t>(
            static_cast<std::int64_t>(damage_.x) + damage_.width,
            static_cast<std::int64_t>(clipped.x) + clipped.width);
        const std::int64_t bottom = std::max<std::int64_t>(
            static_cast<std::int64_t>(damage_.y) + damage_.height,
            static_cast<std::int64_t>(clipped.y) + clipped.height);
        damage_ = Rect{static_cast<std::int32_t>(left),
                       static_cast<std::int32_t>(top),
                       static_cast<std::uint32_t>(right - left),
                       static_cast<std::uint32_t>(bottom - top)};
    }
    return true;
}

bool FramebufferDisplay::present() noexcept {
    if (!valid_) return false;
    if (has_damage_ && flush_ != nullptr) flush_(flush_context_, damage_);
    has_damage_ = false;
    damage_ = {};
    return true;
}

}  // namespace fahim::ui
