#include "fahim/ui/Compositor.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace fahim::ui {

Compositor::Compositor(DisplayDevice& display, const LockScreenSessionGate& gate,
                       std::uint32_t* scratch_argb,
                       std::size_t scratch_pixels) noexcept
    : display_(display), gate_(gate), scratch_(scratch_argb),
      scratch_pixels_(scratch_pixels) {}

bool Compositor::valid() const noexcept {
    if (!display_.valid() || scratch_ == nullptr) return false;
    const std::uint64_t count = static_cast<std::uint64_t>(display_.width()) *
                                display_.height();
    return count != 0u && count <= scratch_pixels_ &&
           count <= std::numeric_limits<std::size_t>::max();
}

bool Compositor::upsert_surface(const Surface& surface) noexcept {
    if (surface.id == 0u || surface.argb_pixels == nullptr ||
        surface.width == 0u || surface.height == 0u ||
        surface.width > 16384u || surface.height > 16384u ||
        surface.stride_pixels < surface.width) return false;

    for (std::size_t i = 0; i < count_; ++i) {
        if (surfaces_[i].id == surface.id) {
            surfaces_[i] = surface;
            return true;
        }
    }
    if (count_ >= surfaces_.size()) return false;
    surfaces_[count_++] = surface;
    return true;
}

bool Compositor::remove_surface(std::uint32_t id) noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        if (surfaces_[i].id != id) continue;
        for (std::size_t j = i + 1; j < count_; ++j) surfaces_[j - 1] = surfaces_[j];
        surfaces_[--count_] = {};
        return true;
    }
    return false;
}

std::uint32_t Compositor::blend(std::uint32_t source,
                               std::uint32_t destination) noexcept {
    const std::uint32_t alpha = source >> 24u;
    if (alpha == 255u) return source;
    if (alpha == 0u) return destination;

    const std::uint32_t inverse = 255u - alpha;
    const auto channel = [alpha, inverse](std::uint32_t src,
                                          std::uint32_t dst) noexcept {
        return (src * alpha + dst * inverse + 127u) / 255u;
    };
    const std::uint32_t red = channel((source >> 16u) & 0xffu,
                                      (destination >> 16u) & 0xffu);
    const std::uint32_t green = channel((source >> 8u) & 0xffu,
                                        (destination >> 8u) & 0xffu);
    const std::uint32_t blue = channel(source & 0xffu, destination & 0xffu);
    return 0xff000000u | (red << 16u) | (green << 8u) | blue;
}

bool Compositor::compose() noexcept {
    if (!valid()) return false;
    const std::uint32_t screen_width = display_.width();
    const std::uint32_t screen_height = display_.height();
    const std::size_t pixels = static_cast<std::size_t>(screen_width) * screen_height;
    const std::uint32_t background =
        0xff000000u | (static_cast<std::uint32_t>(background_.red) << 16u) |
        (static_cast<std::uint32_t>(background_.green) << 8u) |
        static_cast<std::uint32_t>(background_.blue);
    for (std::size_t i = 0; i < pixels; ++i) scratch_[i] = background;

    std::array<std::size_t, kMaxSurfaces> order{};
    std::size_t visible_count = 0;
    for (std::size_t i = 0; i < count_; ++i) {
        if (surfaces_[i].visible && gate_.can_present(surfaces_[i].role)) {
            order[visible_count++] = i;
        }
    }
    for (std::size_t i = 1; i < visible_count; ++i) {
        const std::size_t current = order[i];
        std::size_t j = i;
        while (j > 0 && surfaces_[order[j - 1]].z > surfaces_[current].z) {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = current;
    }

    for (std::size_t n = 0; n < visible_count; ++n) {
        const Surface& surface = surfaces_[order[n]];
        const std::int64_t left = std::max<std::int64_t>(0, surface.x);
        const std::int64_t top = std::max<std::int64_t>(0, surface.y);
        const std::int64_t right = std::min<std::int64_t>(
            screen_width, static_cast<std::int64_t>(surface.x) + surface.width);
        const std::int64_t bottom = std::min<std::int64_t>(
            screen_height, static_cast<std::int64_t>(surface.y) + surface.height);
        if (left >= right || top >= bottom) continue;

        for (std::int64_t y = top; y < bottom; ++y) {
            const std::uint32_t* source_row = surface.argb_pixels +
                static_cast<std::size_t>(y - surface.y) * surface.stride_pixels;
            const std::size_t destination_row =
                static_cast<std::size_t>(y) * screen_width;
            for (std::int64_t x = left; x < right; ++x) {
                const std::uint32_t source_pixel =
                    source_row[static_cast<std::size_t>(x - surface.x)];
                if ((source_pixel >> 24u) == 0u) continue;
                const std::size_t destination_index =
                    destination_row + static_cast<std::size_t>(x);
                scratch_[destination_index] =
                    blend(source_pixel, scratch_[destination_index]);
            }
        }
    }

    for (std::uint32_t y = 0; y < screen_height; ++y) {
        for (std::uint32_t x = 0; x < screen_width; ++x) {
            const std::uint32_t argb =
                scratch_[static_cast<std::size_t>(y) * screen_width + x];
            const Color color{
                static_cast<std::uint8_t>((argb >> 16u) & 0xffu),
                static_cast<std::uint8_t>((argb >> 8u) & 0xffu),
                static_cast<std::uint8_t>(argb & 0xffu),
                255u
            };
            if (!display_.draw_pixel(static_cast<std::int32_t>(x),
                                     static_cast<std::int32_t>(y), color)) return false;
        }
    }
    return display_.present();
}

}  // namespace fahim::ui
